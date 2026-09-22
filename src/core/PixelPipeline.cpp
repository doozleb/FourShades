#include "core/PixelPipeline.h"

#include "core/Ppu.h"

namespace fourshades {

u8 shadeFor(u8 palette, u8 colour) {
    return static_cast<u8>((palette >> (colour * 2)) & 0x03);
}

void PixelPipeline::startLine(const Ppu& ppu) {
    step_ = Step::Tile;
    stepDots_ = 0;
    fetcherX_ = 0;
    discardFetch_ = true;
    queueSize_ = 0;
    queueHead_ = 0;
    pixelX_ = 0;
    discard_ = ppu.scx() & 0x07;
    window_ = false;
    windowSkip_ = 0;
    objects_ = {};
    objectDots_ = 0;
    drawn_ = 0;
    lastPenaltyTile_ = 0;
    lastPenaltyTileValid_ = false;
    objectPenaltyStarted_ = false;
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
        if (windowSkip_ > 0) {
            // The window pixels that fall off the left edge (see the WX < 7
            // note in stepDot) are dropped here, as the tile is pushed,
            // rather than emitted and thrown away: they take no dots.
            const int drop = windowSkip_ < queueSize_ ? windowSkip_ : queueSize_;
            queueHead_ = (queueHead_ + drop) % 8;
            queueSize_ -= drop;
            windowSkip_ -= drop;
        }
        ++fetcherX_;
        step_ = Step::Tile;
        return;
    }
}

void PixelPipeline::startObject(const Ppu& ppu, std::size_t index) {
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

    objectDots_ = objectPenalty(ppu, index, lastPenaltyTile_, lastPenaltyTileValid_,
                                objectPenaltyStarted_);
}

int PixelPipeline::objectPenalty(const Ppu& ppu, std::size_t index, int& lastTile,
                                 bool& lastTileValid, bool& penaltyStarted) const {
    const Ppu::Object& object = ppu.lineObjects()[index];
    // Pan Docs "OBJ Penalty Algorithm", applied to The Pixel - the object's
    // leftmost pixel, at screen X = OAM X - 8, which is off the left edge for
    // an OAM X below 8 but still picks a tile and still costs dots:
    //   - the tile The Pixel is within, and the count of that tile's pixels
    //     strictly to its right minus 2 (zero if negative), the first time an
    //     object lands in that tile;
    //   - a flat six dots for fetching the object's own tile.
    // Both terms are taken in background coordinates, SCX included, so an
    // object off the left edge uses its true negative position rather than
    // the clamped pixelX_ it happens to trigger on: hardware charges objects
    // at OAM X = 0-7 exactly what their own X mod 8 says, and charges an
    // object at OAM X = 0 and one at OAM X = 8 two separate tile terms.
    //
    // Hardware then charges three dots less per line than that sum, once, for
    // the first object fetched on the line. Both findings come from the
    // hardware-verified object timing ROM; see docs/known-divergences.md,
    // "OBJ penalty: the first object fetched on a line gets a three-dot
    // rebate against Pan Docs' algorithm".
    const int backgroundX = static_cast<int>(ppu.scx()) + static_cast<int>(object.x) - 8;
    int dots = 6;
    // NOTE: this tile index is in background coordinates (SCX + the object's
    // own X). Once the window is drawing, tile boundaries actually follow
    // WX - 7 instead, so on a line with both a window and an object this term
    // can be wrong by up to 5 dots. Left for the hardware timing tests in a
    // later task to arbitrate; see docs/known-divergences.md, "OBJ penalty:
    // the tile term ignores the window", for the evidence and how to record
    // the resolution once they do.
    const int penaltyTile = backgroundX >> 3;
    if (!lastTileValid || penaltyTile != lastTile) {
        lastTile = penaltyTile;
        lastTileValid = true;
        if (object.x == 0) {
            // Pan Docs: "an OBJ with an OAM X position of 0 always incurs a
            // 11-dot penalty, regardless of SCX". It is the tile term that
            // the exception replaces, not the whole penalty: ten objects at
            // OAM X = 0 cost 11 + 9 x 6, not 10 x 11, because the second one
            // onwards finds its tile already considered. At SCX = 0 the
            // general rule below gives 11 anyway, and no hardware measurement
            // available here reaches an OAM X = 0 object at a non-zero SCX,
            // so Pan Docs stands.
            dots = 11;
        } else {
            const int toTheRight = 7 - (backgroundX & 7);
            dots += toTheRight > 2 ? toTheRight - 2 : 0;
        }
    }
    if (!penaltyStarted) {
        penaltyStarted = true;
        dots -= 3;
    }
    return dots;
}

int PixelPipeline::dotsRemaining(const Ppu& ppu) const {
    // One dot per pixel still to be emitted, plus the stall the fetch in
    // progress still owes, plus the stalls the object fetches still to come
    // will owe. Nothing else can hold a pixel up over the end of a line: the
    // SCX discard is spent in the line's first eight dots, and the fetcher
    // feeds eight pixels per six-dot fetch, so it is always ahead by then.
    int dots = 160 - pixelX_ + objectDots_;
    if ((ppu.lcdc() & 0x02) == 0) {
        return dots; // objects disabled: none of them will be fetched
    }

    // Objects are fetched in the order the pixel counter reaches them, with
    // ties broken by OAM order (the order lineObjects() is in), and one
    // object's penalty depends on the tile the objects before it claimed -
    // so they have to be walked in that order, over a copy of the memo.
    struct Pending {
        int triggerX = 0;
        std::size_t index = 0;
    };
    std::array<Pending, 10> pending{};
    std::size_t count = 0;
    const auto& list = ppu.lineObjects();
    for (std::size_t i = 0; i < list.size() && count < pending.size(); ++i) {
        if ((drawn_ & (1u << i)) != 0) {
            continue; // already fetched
        }
        // stepDot triggers an object when the pixel counter reaches its
        // screen X, and triggers every object left of the screen edge at
        // pixel 0; one that is already behind the counter never triggers.
        const int screenX = static_cast<int>(list[i].x) - 8;
        const int triggerX = screenX < 0 ? 0 : screenX;
        if (triggerX < pixelX_ || triggerX >= 160) {
            continue;
        }
        pending[count++] = Pending{triggerX, i};
    }
    for (std::size_t i = 1; i < count; ++i) { // insertion sort, stable
        const Pending key = pending[i];
        std::size_t j = i;
        while (j > 0 && pending[j - 1].triggerX > key.triggerX) {
            pending[j] = pending[j - 1];
            --j;
        }
        pending[j] = key;
    }

    int tile = lastPenaltyTile_;
    bool tileValid = lastPenaltyTileValid_;
    bool started = objectPenaltyStarted_;
    for (std::size_t i = 0; i < count; ++i) {
        const int penalty = objectPenalty(ppu, pending[i].index, tile, tileValid, started);
        if (penalty > 0) {
            dots += penalty;
        }
    }
    return dots;
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
        // WX = 7 lines the window's first pixel up with screen x = 0, so a
        // smaller WX pushes 7 - WX of them off the left edge: the fetcher
        // still starts at the window's own column 0 and those pixels never
        // reach the LCD. Mealybug Tearoom's m3_wx_4_change and
        // m3_wx_5_change photograph the three and two pixel versions of it.
        // Dropping them here, as the tile is pushed, makes them cost no
        // dots. That half is a tuning decision, not a measurement: the test
        // that would arbitrate it, m3_window_timing, still fails on the very
        // lines that measure it, and neither this placement nor charging a
        // dot each reproduces what its reference shows. See
        // docs/known-divergences.md, "A WX below 7 pushes the window's
        // leftmost pixels off the screen".
        windowSkip_ = ppu.wx() < 7 ? 7 - static_cast<int>(ppu.wx()) : 0;
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
