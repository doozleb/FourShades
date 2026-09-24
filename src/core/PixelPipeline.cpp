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
    fetchWindow_ = false;
    windowX_ = 0;
    windowXHeadStart_ = false;
    windowComparedX_ = -1;
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
            fetchWindow_ = window;
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
        if (windowSkip_ > 0 && !fetchWindow_) {
            // The clip is the window's: it exists because the window's first
            // tile starts kWindowCounterHeadStart - WX pixels left of screen
            // x = 0. This tile is a background one, so a cleared LCDC bit 5
            // reached the fetcher before it read the map (see
            // stopWindowIfDisabled) and the window tile the clip was owed to
            // never arrived. The background carries on from where it was, so
            // it is not clipped, and nothing is left owing: a later activation
            // on this line sets the clip again from the WX it matched.
            windowSkip_ = 0;
        } else if (windowSkip_ > 0) {
            // The window pixels that fall off the left edge (see the WX
            // note in startWindow) are dropped here, as the tile is pushed,
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
    // can be wrong by up to 5 dots. Left for the hardware timing tests to
    // arbitrate; see docs/known-divergences.md, "OBJ penalty: the first
    // object fetched on a line gets a three-dot rebate against Pan Docs'
    // algorithm", and its "the tile term ignores the window" limitation, for
    // the evidence and how to record the resolution once they do.
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

int PixelPipeline::fetchStallDots() const {
    // Dots the fetcher still owes before its Push step runs again. Tile,
    // DataLow and DataHigh take two dots each (stepFetcher), and Push emits
    // its first pixel on the dot it runs, so that dot is not counted here -
    // it is one of the per-pixel dots the caller counts.
    int dots = 0;
    switch (step_) {
    case Step::Tile:
        dots = 6 - stepDots_;
        break;
    case Step::DataLow:
        dots = 4 - stepDots_;
        break;
    case Step::DataHigh:
        dots = 2 - stepDots_;
        break;
    case Step::Push:
        dots = 0;
        break;
    }
    if (discardFetch_) {
        dots += 6; // the line's first fetch is thrown away and run again
    }
    return dots;
}

int PixelPipeline::dotsRemaining(const Ppu& ppu) const {
    // One dot per pixel still to be emitted, plus the stall the fetch in
    // progress still owes, plus the stalls the object fetches still to come
    // will owe. The SCX discard is spent in the line's first eight dots, and
    // the fetcher feeds eight pixels per six-dot fetch, so it stays ahead of
    // the pixel counter on its own - except across a window activation,
    // which is not a "keeping up" fetch but a full restart (see
    // kWindowRestartDots), so it is charged separately below, both while it
    // is still to come and while it is running.
    int dots = 160 - pixelX_ + objectDots_;
    if (queueSize_ == 0 && pixelX_ < 160) {
        // Nothing is queued, so no pixel can be emitted until the fetcher
        // pushes again: those dots are on top of the one-per-pixel count.
        // This is what charges the rest of a window restart already in
        // progress - stepDot has cleared the queue and put the fetcher back
        // at its Tile step, and five more dots pass before the pre-empted
        // pixel is emitted. In ordinary running the queue only empties on
        // the dot before a Push, where this adds nothing.
        dots += fetchStallDots();
    }
    if (windowConditions(ppu)) {
        // The window has not started on this line yet, but stepDot will clear
        // the queue and restart the fetcher as soon as the X counter is at or
        // past WX - on the dot the counter reaches it, or on the very next dot
        // if it is already past (LCDC bit 5 set mid-line, or a WX left of the
        // first pixel). Either way the activation is still to come and still
        // on this line, and the dots counted above already include one dot for
        // the pixel it pre-empts, so its full fetch cost is added on top of
        // them, not folded in.
        //
        // The counter reads kWindowCounterHeadStart before pixel 0 and one
        // more per pixel rendered, so kWindowCounterHeadStart + 159 is the
        // largest value it takes while a pixel is still to be emitted: a WX
        // above that is never matched on this line. Nor is one the counter has
        // already gone past, because the comparison is an equality - so the
        // activation is only still to come while WX is a value the counter has
        // yet to *test*. The counter's own current value still counts as one of
        // those: stepDot compares at the top of a dot and increments at the
        // bottom of it, and this runs in between, so a counter that has just
        // reached WX is matched on the next dot, not this one.
        const int wx = static_cast<int>(ppu.wx());
        if (wx >= windowX_ && wx <= kWindowCounterHeadStart + 159) {
            dots += kWindowRestartDots;
        }
    }
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

bool PixelPipeline::windowEnabled(const Ppu& ppu) const {
    // Pan Docs, "Window behavior": a counter match acts only "if the Y
    // condition is true and the Window enable bit is set in LCDC". Both are
    // read here, on the dot the match is tested, not once per line.
    return (ppu.lcdc() & 0x20) != 0 && ppu.windowReached();
}

bool PixelPipeline::windowConditions(const Ppu& ppu) const {
    // The third term is that the window is not already drawing, which is the
    // one thing the equality comparison cannot express on its own; see window_
    // in the header for why nothing else is needed, and why a bare re-enable
    // therefore does nothing without a WX that moved ahead of the counter.
    return !window_ && windowEnabled(ppu);
}

void PixelPipeline::startWindow(Ppu& ppu) {
    // Pan Docs: "background rendering is reset, beginning anew from the active
    // row of the Window's tilemap" - the background queue is cleared and the
    // fetcher restarts, which costs the documented kWindowRestartDots dots.
    window_ = true;
    queueSize_ = 0;
    queueHead_ = 0;
    step_ = Step::Tile;
    stepDots_ = 0;
    fetcherX_ = 0;
    // A WX of kWindowCounterHeadStart is matched by the last of the counter's
    // free increments, so the window's first pixel lands on screen x = 0. A
    // smaller WX is matched earlier among them, and the free increments left
    // over after the match are pixels the window draws and the LCD never
    // shows: kWindowCounterHeadStart - WX of them, off the left edge. The
    // fetcher still starts at the window's own column 0. Two Mealybug Tearoom
    // references, photographed from DMG hardware, show the three and two
    // pixel versions of it. Dropping them at the push, so that they cost no
    // dots, is a tuning decision, not a measurement: the test that would
    // arbitrate the dot cost still fails on the very lines that measure it,
    // and neither this placement nor charging a dot each reproduces what its
    // reference shows. See docs/known-divergences.md, "A WX below 7 pushes
    // the window's leftmost pixels off the screen", for the ROMs and the
    // figures.
    windowSkip_ = static_cast<int>(ppu.wx()) < kWindowCounterHeadStart
                      ? kWindowCounterHeadStart - static_cast<int>(ppu.wx())
                      : 0;
    // Cache the row the window is drawing on this line before advancing the
    // PPU's counter for the next one: every fetch reads windowLineUsed_,
    // never ppu.windowLine() directly, so the just-bumped value doesn't leak
    // into this line's tiles.
    windowLineUsed_ = ppu.windowLine();
    // This counter value has now been compared; see windowComparedX_.
    windowComparedX_ = windowX_;
    // Pan Docs: "The coordinate of the active Window row is then
    // incremented." It is the activation that advances it, not the line: a
    // line that never matched WX leaves it where it was, and a line that
    // matched twice advances it twice, so the second band draws the row after
    // the first - Mealybug's "it will start drawing the next row of the
    // window, on the same scanline". Reading windowLineUsed_ just above,
    // before the advance, is what makes that come out right for both bands.
    ppu.advanceWindowLine();
}

void PixelPipeline::pushWindowShiftPixel() {
    // See the header for Pan Docs' sentence and the two things it leaves to be
    // measured. Marked as compared whether or not the FIFO had room, because the
    // comparator fired either way.
    windowComparedX_ = windowX_;
    if (queueSize_ != 0) {
        return; // the FIFO's push port takes a push only when it is empty
    }
    queue_[static_cast<std::size_t>(queueHead_)] = 0;
    queueSize_ = 1;
}

void PixelPipeline::takeWindowHeadStart(Ppu& ppu) {
    // The counter's free increments, with the equality test against WX applied
    // at each of them, which is how a WX below kWindowCounterHeadStart starts
    // the window before the line's first pixel is rendered. They are taken on
    // the dot the SCX discard finishes rather than at the top of the line:
    // this model spends the discard as emitted-and-dropped pixels over the
    // line's first dots, where the hardware's free increments are the
    // discard, and taking them here is what keeps the trigger on the dot the
    // arithmetic this replaced put it on. Straightening that out is the task
    // that retires windowSkip_ and gives WX = 0 its SCX % 8 shift.
    windowXHeadStart_ = true;
    while (true) {
        // The comparison is made at every value the counter takes, 0 to
        // kWindowCounterHeadStart inclusive - the last of them is the value a
        // WX of kWindowCounterHeadStart matches, which starts the window with
        // its first pixel on screen x = 0.
        if (windowConditions(ppu) && windowX_ == static_cast<int>(ppu.wx())) {
            startWindow(ppu);
        }
        if (windowX_ == kWindowCounterHeadStart) {
            break;
        }
        ++windowX_;
    }
    // All of those values have now been compared against WX, on this one dot,
    // so a WX written later cannot be "reached again" at any of them. Set after
    // the loop, because a startWindow inside it leaves the value it matched
    // here and that is the lower number. See windowComparedX_.
    windowComparedX_ = kWindowCounterHeadStart;
}

void PixelPipeline::stopWindowIfDisabled(const Ppu& ppu) {
    // See the header for the two sentences of Mealybug's notes this is.
    // Clearing window_ is what lets the window activate again later on the
    // line - but only if WX moves ahead of the counter first, because the
    // comparison in stepDot is an equality and the counter only counts up.
    if (window_ && (ppu.lcdc() & 0x20) == 0) {
        window_ = false;
    }
}

bool PixelPipeline::stepDot(Ppu& ppu, std::array<u8, 160>& line) {
    // The nesting matters: while the free increments are still owed and the
    // SCX discard has not drained, the counter reads 0 and must not be
    // compared against WX at all. Folding the two conditions into one `&&`
    // would let the catch-up branch below see that 0 and start the window on
    // the line's first dot for WX = 0.
    if (!windowXHeadStart_) {
        if (discard_ == 0) {
            takeWindowHeadStart(ppu);
        }
    } else if (windowEnabled(ppu) && windowX_ == static_cast<int>(ppu.wx())) {
        // Pan Docs: "When this counter is equal to WX ... background rendering
        // is reset". An equality, tested on every dot, and every match that
        // finds the window not already drawing is an activation - "this
        // process can happen more than once per scanline". A counter that has
        // gone past WX is therefore not a match: a window enabled late, or a
        // WX lowered behind the counter, does not start on that line, and
        // nothing on the line after a stop restarts the window until WX is
        // moved ahead of the counter again.
        if (!window_) {
            startWindow(ppu);
        } else if (windowX_ > windowComparedX_) {
            // The window is already drawing, so this is a WX that moved ahead
            // of the counter mid-window rather than a second activation: it
            // pushes one colour-0 pixel and leaves the window's row alone.
            pushWindowShiftPixel();
        }
    }
    // After the activation test, so that a line whose LCDC bit 5 is clear
    // throughout cannot start the window and stop it on the same dot, and
    // before the fetcher runs, so the tile it is working on when bit 5 goes
    // low is already a background tile.
    stopWindowIfDisabled(ppu);

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
            // Pan Docs: the X counter is "incremented for each pixel
            // rendered". The SCX discard above is not a rendered pixel, so it
            // does not increment it.
            ++windowX_;
        }
    }
    return pixelX_ >= 160;
}

} // namespace fourshades
