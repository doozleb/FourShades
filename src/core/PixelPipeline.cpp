#include "core/PixelPipeline.h"

#include "core/Ppu.h"

namespace fourshades {

u8 shadeFor(u8 palette, u8 colour) {
    return static_cast<u8>((palette >> (colour * 2)) & 0x03);
}

void PixelPipeline::startLine(Ppu& ppu) {
    step_ = Step::Tile;
    stepDots_ = 0;
    fetcherX_ = 0;
    discardFetch_ = true;
    queueSize_ = 0;
    queueHead_ = 0;
    pixelX_ = 0;
    discard_ = ppu.scx() & 0x07;
    window_ = false;
    objects_ = {};
    objectDots_ = 0;
    drawn_ = 0;
    lastPenaltyTile_ = -1;
}

u16 PixelPipeline::tileRowAddress(const Ppu& ppu) const {
    const u8 y = window_ ? static_cast<u8>(windowLineUsed_)
                          : static_cast<u8>(ppu.lineNumber() + ppu.scy());
    const u16 base = (ppu.lcdc() & 0x10) != 0
                         ? static_cast<u16>(0x8000 + tileIndex_ * 16)
                         : static_cast<u16>(0x9000 + static_cast<i8>(tileIndex_) * 16);
    return static_cast<u16>(base + (y & 7) * 2);
}

void PixelPipeline::stepFetcher(const Ppu& ppu) {
    switch (step_) {
    case Step::Tile:
        if (++stepDots_ < 2) {
            return;
        }
        stepDots_ = 0;
        {
            const bool window = window_;
            const u16 map = (window ? (ppu.lcdc() & 0x40) : (ppu.lcdc() & 0x08)) != 0 ? 0x9C00 : 0x9800;
            const u8 y = window ? static_cast<u8>(windowLineUsed_)
                                 : static_cast<u8>(ppu.lineNumber() + ppu.scy());
            const u8 x = window ? static_cast<u8>(fetcherX_ & 0x1F)
                                 : static_cast<u8>(((ppu.scx() / 8) + fetcherX_) & 0x1F);
            tileIndex_ = ppu.peekVram(static_cast<u16>(map + (y / 8) * 32 + x));
        }
        step_ = Step::DataLow;
        return;
    case Step::DataLow:
        if (++stepDots_ < 2) {
            return;
        }
        stepDots_ = 0;
        tileLow_ = ppu.peekVram(tileRowAddress(ppu));
        step_ = Step::DataHigh;
        return;
    case Step::DataHigh:
        if (++stepDots_ < 2) {
            return;
        }
        stepDots_ = 0;
        tileHigh_ = ppu.peekVram(static_cast<u16>(tileRowAddress(ppu) + 1));
        if (discardFetch_) {
            // Pan Docs: two tile fetches happen before the first pixel. The
            // first one's result is thrown away with no pixels queued, so it
            // costs exactly the 6 dots of Tile+DataLow+DataHigh above, not a
            // seventh dot for a Push step that has nothing to push.
            discardFetch_ = false;
            step_ = Step::Tile;
            return;
        }
        step_ = Step::Push;
        return;
    case Step::Push:
        if (queueSize_ != 0) {
            return; // retried every dot until the queue drains
        }
        for (int bit = 7; bit >= 0; --bit) {
            const u8 low = static_cast<u8>((tileLow_ >> bit) & 1);
            const u8 high = static_cast<u8>((tileHigh_ >> bit) & 1);
            queue_[static_cast<std::size_t>((queueHead_ + queueSize_) % 8)] =
                static_cast<u8>((high << 1) | low);
            ++queueSize_;
        }
        ++fetcherX_;
        step_ = Step::Tile;
        return;
    }
}

void PixelPipeline::startObject(Ppu& ppu, std::size_t index) {
    // PixelPipeline.h only forward-declares Ppu, so the object is reached
    // through the index rather than named in the header.
    const Ppu::Object& object = ppu.lineObjects()[index];
    const int height = ppu.objectHeight();
    int row = ppu.lineNumber() - (static_cast<int>(object.y) - 16);
    if ((object.flags & 0x40) != 0) { // Y flip
        row = height - 1 - row;
    }
    const u8 tile = height == 16 ? static_cast<u8>(object.tile & 0xFE) : object.tile;
    const u16 address = static_cast<u16>(0x8000 + tile * 16 + row * 2);
    const u8 low = ppu.peekVram(address);
    const u8 high = ppu.peekVram(static_cast<u16>(address + 1));

    const int screenX = static_cast<int>(object.x) - 8;
    for (int i = 0; i < 8; ++i) {
        const int x = screenX + i;
        if (x < pixelX_ || x >= pixelX_ + 8) {
            continue; // outside the eight pixels the queue covers
        }
        const int bit = (object.flags & 0x20) != 0 ? i : 7 - i; // X flip
        const u8 colour = static_cast<u8>((((high >> bit) & 1) << 1) | ((low >> bit) & 1));
        ObjectPixel& slot = objects_[static_cast<std::size_t>(x - pixelX_)];
        // Pan Docs' documented DMG priority rule ("OAM: Drawing priority"):
        // the object with the smaller X coordinate wins, and ties (equal X)
        // are broken by the lower OAM index. This is an approximation of
        // that rule, not an implementation of it: the first object to claim
        // a pixel wins, full stop. It usually agrees, because objects are
        // started left to right as pixelX_ reaches each one's screen X, so
        // the first claimant is usually the one with the smallest X, and two
        // objects sharing an X both trigger on the same dot and get scanned
        // in OAM order, which matches the documented tie-break. It differs
        // for objects at or left of the screen edge (X = 1-8): they all
        // trigger together at pixelX_ == 0 regardless of their true relative
        // X, so among those the winner is whichever this OAM-order scan
        // reaches first, not necessarily the one with the smallest X.
        // Hardware-verified image-comparison test ROMs that probe overlapping
        // objects directly would arbitrate this; see docs/known-divergences.md
        // for the decision, the evidence and which ROMs those are.
        if (colour != 0 && slot.colour == 0) { // the first object to claim it wins
            slot = ObjectPixel{colour, static_cast<u8>((object.flags & 0x10) != 0 ? 1 : 0),
                               (object.flags & 0x80) != 0};
        }
    }

    // Pan Docs "OBJ penalty algorithm": a flat six dots, plus the wait for the
    // background fetch of the tile the object's leftmost pixel falls in, the
    // first time an object lands in that tile. X = 0 always costs eleven,
    // which is exactly the flat six plus the full tile term, so this object
    // has effectively already paid for the tile at pixelX_ (always tile 0 of
    // the discard-adjusted grid, since X = 0 only ever triggers at pixelX_ ==
    // 0): record that before returning, or a second object triggering at
    // pixelX_ == 0 would pay the tile term again.
    if (object.x == 0) {
        objectDots_ = 11;
        lastPenaltyTile_ = (ppu.scx() + pixelX_) / 8;
        return;
    }
    objectDots_ = 6;
    // NOTE: this tile index is in background coordinates (SCX + pixelX_).
    // Once the window is drawing, tile boundaries actually follow WX - 7
    // instead, so on a line with both a window and an object this term can
    // be wrong by up to 5 dots. Left for the hardware timing tests in a
    // later task to arbitrate; see docs/known-divergences.md conventions
    // for how to record the resolution once they do.
    const int tileOfPixel = (ppu.scx() + pixelX_) / 8;
    if (tileOfPixel != lastPenaltyTile_) {
        lastPenaltyTile_ = tileOfPixel;
        const int toTheRight = 7 - ((ppu.scx() + pixelX_) & 7);
        objectDots_ += toTheRight > 2 ? toTheRight - 2 : 0;
    }
}

bool PixelPipeline::stepDot(Ppu& ppu, std::array<u8, 160>& line) {
    if (!window_ && (ppu.lcdc() & 0x20) != 0 && ppu.windowReached() &&
        discard_ == 0 && pixelX_ >= static_cast<int>(ppu.wx()) - 7) {
        // Pan Docs: the background queue is cleared and the fetcher restarts,
        // which costs the documented six dots.
        window_ = true;
        queueSize_ = 0;
        queueHead_ = 0;
        step_ = Step::Tile;
        stepDots_ = 0;
        fetcherX_ = 0;
        // Cache the row the window is drawing on this line before advancing
        // the PPU's counter for the next one: every fetch below reads
        // windowLineUsed_, never ppu.windowLine() directly, so the
        // just-bumped value doesn't leak into this line's tiles.
        windowLineUsed_ = ppu.windowLine();
        // The window's line counter only advances on lines that drew it.
        ppu.advanceWindowLine();
    }

    if (objectDots_ > 0) {
        --objectDots_;   // the fetch stalls the pixel stream
        return false;
    }
    if ((ppu.lcdc() & 0x02) != 0) {
        const auto& list = ppu.lineObjects();
        for (std::size_t i = 0; i < list.size(); ++i) {
            if ((drawn_ & (1u << i)) != 0) {
                continue;
            }
            const int screenX = static_cast<int>(list[i].x) - 8;
            if (screenX != pixelX_ && !(screenX < 0 && pixelX_ == 0)) {
                continue;
            }
            drawn_ |= 1u << i;
            startObject(ppu, i);
            if (objectDots_ > 0) {
                --objectDots_;
                return false;
            }
        }
    }

    stepFetcher(ppu);
    if (queueSize_ > 0) {
        const u8 background = queue_[static_cast<std::size_t>(queueHead_)];
        queueHead_ = (queueHead_ + 1) % 8;
        --queueSize_;
        if (discard_ > 0) {
            // These are background pixels scrolled off the left edge by SCX;
            // pixelX_ does not advance for them, so objects_ (whose
            // invariant is "objects_[k] holds the object pixel for screen
            // pixel pixelX_ + k") must not shift either, or it desyncs from
            // pixelX_ and an object merged before the discard drains (any
            // object at screen X <= 0) renders shifted left by the discard
            // count once emission actually starts.
            --discard_;
        } else {
            const ObjectPixel object = objects_[0];
            for (std::size_t i = 0; i + 1 < objects_.size(); ++i) {
                objects_[i] = objects_[i + 1];
            }
            objects_[objects_.size() - 1] = ObjectPixel{};
            const u8 bgColour = (ppu.lcdc() & 0x01) != 0 ? background : 0;
            u8 shade = shadeFor(ppu.bgp(), bgColour);
            const bool objectWins = object.colour != 0 && (ppu.lcdc() & 0x02) != 0 &&
                                    (!object.behind || bgColour == 0);
            if (objectWins) {
                shade = shadeFor(ppu.obp(object.palette), object.colour);
            }
            line[static_cast<std::size_t>(pixelX_)] = shade;
            ++pixelX_;
        }
    }
    return pixelX_ >= 160;
}

} // namespace fourshades
