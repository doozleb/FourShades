#include "core/PixelPipeline.h"

#include "core/Ppu.h"

namespace fourshades {

u8 shadeFor(u8 palette, u8 colour) {
    return static_cast<u8>((palette >> (colour * 2)) & 0x03);
}

void PixelPipeline::startLine(const Ppu& ppu) {
    step_ = Step::Tile;
    stepDots_ = 0;
    pushed_ = false;
    fetchReset_ = true; // the line's first dot is the reset's; see fetchReset_
    fetcherX_ = 0;
    discardFetch_ = true;
    queueSize_ = 0;
    queueHead_ = 0;
    pixelX_ = 0;
    discard_ = 0; // latched by the line's first tile fetch; see stepFetcher
    window_ = false;
    fetchWindow_ = false;
    windowX_ = 0;
    windowXHeadStart_ = false;
    windowComparedX_ = -1;
    windowCounterLead_ = kWindowCounterLeadDots;
    wxPipe_.fill(ppu.wx());
    windowRendering_ = false;
    objects_ = {};
    objectDots_ = 0;
    objectFetching_ = false;
    objectLeadDots_ = 0;
    fifoLead_ = 0;
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

void PixelPipeline::tryPushRow() {
    if (queueSize_ > fifoLead_) {
        // One push port, and it takes a row only when there is room for one.
        // Pan Docs says "pixels are only pushed to the background FIFO if it's
        // empty", which is this with fifoLead_ = 0 - every line without an
        // object. Once the fetcher has taken its lead at the line's first
        // object fetch (see kObjectFetcherLead) the pixels it is ahead by are
        // still in the FIFO when the next row is ready, and the sixteen-pixel
        // hardware FIFO has room for both.
        return;
    }
    const int capacity = static_cast<int>(queue_.size());
    for (int bit = 7; bit >= 0; --bit) {
        const u8 low = static_cast<u8>((tileLow_ >> bit) & 1);
        const u8 high = static_cast<u8>((tileHigh_ >> bit) & 1);
        queue_[static_cast<std::size_t>((queueHead_ + queueSize_) % capacity)] =
            static_cast<u8>((high << 1) | low);
        ++queueSize_;
    }
    if (fetchWindow_) {
        // Pan Docs' pixel FIFO page says the colour-0 pixel is pushed by a
        // WX changed "after the window has started rendering". This is what
        // starting to render is: window pixels in the queue. See
        // windowRendering_ and pushWindowShiftPixel.
        windowRendering_ = true;
    }
    ++fetcherX_;
    pushed_ = true;
}

// Mealybug Tearoom's PPU notes name the stage `B`: the tile-index fetch. Every
// register that goes into its VRAM address is read here, on the stage's first
// dot - see stepFetcher for what pins that dot - which makes this one sample of
// SCX, one of SCY and one of LCDC's map-select bit per fetch.
void PixelPipeline::sampleTileIndex(const Ppu& ppu) {
    const bool window = window_;
    if (discardFetch_) {
        // The fine-scroll discard is the low three bits of the same SCX this
        // stage reads to pick the line's first tile column - one sample serving
        // both - and it is read here rather than when mode 3 begins.
        //
        // Two Mealybug Tearoom references pin the M-cycle. One sweeps the dot
        // it rewrites SCX on, and over the lines where the write lands on the
        // M-cycle ending just before this stage, its photograph follows the new
        // value; reading SCX when mode 3 begins is an M-cycle too early and
        // puts four to six pixels of each of those lines in the wrong place.
        // The other agrees from the other side, on a line where the two values
        // differ by seven. See docs/known-divergences.md, "The fine-scroll
        // discard reads SCX at the line's first tile fetch". Which of the
        // stage's two dots it is read on is the one thing those two do not
        // decide - both give the identical picture either way - so it stays
        // welded to the map column's read.
        discard_ = static_cast<int>(ppu.scx() & 0x07);
    }
    fetchWindow_ = window;
    const u16 map = (window ? (ppu.lcdc() & 0x40) : (ppu.lcdc() & 0x08)) != 0 ? 0x9C00 : 0x9800;
    const u8 y = window ? static_cast<u8>(windowLineUsed_)
                        : static_cast<u8>(ppu.lineNumber() + ppu.scy());
    const u8 x = window ? static_cast<u8>(fetcherX_ & 0x1F)
                        : static_cast<u8>(((ppu.scx() / 8) + fetcherX_) & 0x1F);
    tileIndex_ = ppu.peekVram(static_cast<u16>(map + (y / 8) * 32 + x));
}

void PixelPipeline::stepFetcher(const Ppu& ppu) {
    if (fetchReset_) {
        // The dot a reset costs before Get Tile begins; see fetchReset_.
        fetchReset_ = false;
        return;
    }
    switch (step_) {
    // Each of the three fetch stages below is two dots, and each samples the
    // registers its VRAM address is built from on the **first** of them: the
    // address goes out on the bus on one dot and the byte comes back on the
    // next. That is one dot, and it is the whole of what this task settled.
    //
    // Mealybug Tearoom's PPU notes name the stages a register is read at - SCY
    // at `B`, `0` and `1`, TILE_SEL at `0` and `1`, quoted in
    // docs/known-divergences.md - but not which dot of a stage, and on a plain
    // line nothing can: a write lands at the end of an M-cycle, and every
    // stage's two dots then sit on the same side of every M-cycle boundary. A
    // transparent object's fetch, whose dot cost the OBJ penalty algorithm can
    // make odd, moves the rest of the line's stages across that grid and
    // separates them; four unit cases do exactly that, one per stage plus the
    // bitplane mixing the notes describe for TILE_SEL. The three stages were
    // also measured one at a time against the reference photographs, and all
    // three independently want their first dot - so they share one rule rather
    // than needing three. See docs/known-divergences.md, "Each fetch stage
    // samples its registers on its first dot".
    case Step::Tile:
        if (stepDots_ == 0) {
            sampleTileIndex(ppu);
        }
        if (++stepDots_ < 2) {
            return;
        }
        stepDots_ = 0;
        step_ = Step::DataLow;
        return;
    case Step::DataLow:
        if (stepDots_ == 0) {
            tileLow_ = ppu.peekVram(tileRowAddress(ppu));
        }
        if (++stepDots_ < 2) {
            return;
        }
        stepDots_ = 0;
        step_ = Step::DataHigh;
        return;
    case Step::DataHigh:
        if (stepDots_ == 0) {
            tileHigh_ = ppu.peekVram(static_cast<u16>(tileRowAddress(ppu) + 1));
        }
        if (++stepDots_ < 2) {
            return;
        }
        stepDots_ = 0;
        if (discardFetch_) {
            // Pan Docs: two tile fetches happen before the first pixel. The
            // first one's result is thrown away, so this fetch costs exactly
            // the 6 dots of Tile+DataLow+DataHigh above and none of the Sleep
            // and Push steps below: those are where the fetcher waits for room
            // in the FIFO, and a row that is thrown away has nothing to wait
            // for. Six here and the reset's one dot before it (see
            // fetchReset_) are what put the line's first push on the
            // thirteenth dot of rendering.
            discardFetch_ = false;
            step_ = Step::Tile;
            return;
        }
        // Pan Docs: Get Tile Data High "also pushes a row of
        // background/window pixels to the FIFO. This extra push is not part of
        // the 8 steps, meaning there's 3 total chances to push pixels to the
        // background FIFO every time the complete fetcher steps are performed."
        //
        // This is the chance an undisturbed line uses, every time: a row feeds
        // eight pixels and the whole fetch is eight dots, so the FIFO empties
        // exactly as this step completes. Writing P for the dot the tile's
        // first pixel is drawn on, that puts the push on P, the high bitplane's
        // read on P-1, the low bitplane's on P-3 and the tile index's on P-5 -
        // the same three offsets for every fetch on the line, which is what the
        // dot each register reaches the fetcher on is measured against. See
        // docs/known-divergences.md, "The background fetcher is five steps over
        // eight dots, and the dot that leaves over", for the measurements that
        // pin this phase, and "Each fetch stage samples its registers on its
        // first dot" for the three reads inside it.
        //
        // The other two chances are the Sleep dots below, and they are not
        // decoration: an extra pixel pushed into the FIFO from outside the
        // fetcher (see pushWindowShiftPixel) blocks this one, and the next
        // chance a dot later is what keeps that pixel costing no dots.
        pushed_ = false;
        tryPushRow();
        step_ = Step::Sleep;
        return;
    case Step::Sleep:
        if (!pushed_) {
            tryPushRow(); // chances two and three
        }
        if (++stepDots_ < 2) {
            return;
        }
        stepDots_ = 0;
        // The Sleep dots are spent whether or not the row has gone in, which is
        // what makes a complete fetch eight dots; the Push step below is only
        // reached when all three chances found the FIFO occupied.
        step_ = pushed_ ? Step::Tile : Step::Push;
        return;
    case Step::Push:
        tryPushRow();
        if (pushed_) {
            step_ = Step::Tile; // Get Tile begins on the dot after the push
        }
        return;
    }
}

void PixelPipeline::startObject(const Ppu& ppu, std::size_t index) {
    const bool firstOnLine = !objectPenaltyStarted_;
    objectDots_ = objectPenalty(ppu, index, lastPenaltyTile_, lastPenaltyTileValid_,
                                objectPenaltyStarted_);
    objectIndex_ = index;
    objectFetching_ = true;
    if (firstOnLine) {
        // The line's first object fetch is where the fetcher takes its lead and
        // the FIFO starts carrying it. See kObjectFetcherLead.
        objectLeadDots_ = kObjectFetcherLead;
        fifoLead_ = kObjectFetcherLead;
    }
}

void PixelPipeline::cancelObjectIfDisabled(const Ppu& ppu) {
    if (objectFetching_ && (ppu.lcdc() & 0x02) == 0) {
        objectFetching_ = false; // the dots stay owed; see the header
    }
}

void PixelPipeline::fetchObjectRow(const Ppu& ppu) {
    // PixelPipeline.h only forward-declares Ppu, so the object is reached
    // through the index rather than named in the header.
    const Ppu::Object& object = ppu.lineObjects()[objectIndex_];
    // LCDC bit 2 is read here, on the dot the address is built, not when the
    // fetch was triggered: a write that lands in between changes the object's
    // height under its own fetch, which is what one of the Mealybug Tearoom
    // references photographs.
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

    objectFetching_ = false;
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
    // This is what the pixels pay, in full: a Mealybug Tearoom reference
    // photographs the pixel stream on a line with one object at eighteen
    // different OAM X coordinates and finds it exactly this far behind an
    // object-free line. The three dots the hardware-verified object timing ROM
    // finds mode 3 short of the same sum are not taken off here - they are dots
    // the fetcher keeps while the pixels wait; see kObjectFetcherLead and
    // docs/known-divergences.md, "An object fetch costs the pixels three dots
    // more than it costs the fetcher and mode 3".
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
        // Only which object is the line's first is recorded here; what being
        // first is worth is kObjectFetcherLead, and it is worth it to the
        // fetcher and to mode 3, not to this sum.
        penaltyStarted = true;
    }
    return dots;
}

int PixelPipeline::fetchStallDots() const {
    // Dots the fetcher still owes before the dot its next row reaches the FIFO.
    // Tile, DataLow and DataHigh take two dots each and the row goes in as
    // DataHigh completes (stepFetcher), which is also the dot that row's first
    // pixel is drawn on - so that dot is not counted here: it is one of the
    // per-pixel dots the caller counts.
    int dots = fetchReset_ ? 1 : 0; // the reset's own dot; see fetchReset_
    switch (step_) {
    case Step::Tile:
        dots += 5 - stepDots_;
        break;
    case Step::DataLow:
        dots += 3 - stepDots_;
        break;
    case Step::DataHigh:
        dots += 1 - stepDots_;
        break;
    case Step::Sleep:
    case Step::Push:
        // The row is assembled and a chance to push it comes every dot from
        // here, so on the only dots the caller asks about - the queue empty -
        // it went in on this dot already and nothing is owed.
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
    // the fetcher feeds eight pixels per eight-dot fetch, so it stays ahead of
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
        // those: this runs after stepDot, so windowX_ is already the value the
        // next dot will compare, and a counter that has just reached WX is
        // matched on that dot rather than the one just finished.
        //
        // WX is taken from the comparator's own pipeline rather than the
        // register, for the same reason: what matters is the value the next
        // comparison will see. See kWindowCounterWxLag.
        const int wx = static_cast<int>(wxPipe_.front());
        if (wx >= windowX_ && wx <= kWindowCounterHeadStart + 159) {
            dots += kWindowRestartDots;
        }
    }
    if ((ppu.lcdc() & 0x02) == 0) {
        // Objects disabled: none of them will be fetched. One already fetched
        // still leaves the fetcher, and so mode 3's end, ahead of the pixels.
        return dots - (objectPenaltyStarted_ ? kObjectFetcherLead : 0);
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
    // Mode 3 ends with the fetcher, and a line that fetches an object leaves the
    // fetcher kObjectFetcherLead dots ahead of the pixels - whether it has
    // happened yet or is still to come. The last pixels of such a line reach the
    // LCD that many dots further into HBlank than kRenderLag alone says, which
    // is what keeps the hardware-verified object timing ROM and the picture the
    // Mealybug reference photographs both right. See kObjectFetcherLead.
    return dots - (started ? kObjectFetcherLead : 0);
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
    windowRendering_ = false;
    queueSize_ = 0;
    queueHead_ = 0;
    step_ = Step::Tile;
    stepDots_ = 0;
    pushed_ = false;
    // Pan Docs: "the fetcher is reset to step 1". The reset costs the dot it
    // lands on before that step begins - the same dot the line's own start
    // spends - and it is what makes the restart kWindowRestartDots dots rather
    // than five. See fetchReset_.
    fetchReset_ = true;
    // One fetch, not the two a line begins with: the thrown-away first fetch
    // belongs to the line, and a window that restarts part-way through it does
    // not owe it again. This is also what makes the restart cost
    // kWindowRestartDots wherever on the line it happens, which is what one
    // of the two references measures for a whole spread of WX at once.
    discardFetch_ = false;
    fetcherX_ = 0;
    // A WX of kWindowCounterHeadStart is matched by the last of the counter's
    // free increments, so the window's first pixel lands on screen x = 0. A
    // smaller WX is matched earlier among them, and the free increments left
    // over after the match are pixels the window draws and the LCD never
    // shows: kWindowCounterHeadStart - WX of them, off the left edge.
    //
    // They go into discard_, the same counter the SCX fine-scroll pixels use,
    // and cost a dot each. That is what makes the arithmetic come out: the
    // increments are one per dot, so a match that comes this many dots early
    // has exactly this many pixels to throw away, and the line's first pixel
    // lands in the same place for every WX at or below
    // kWindowCounterHeadStart - six dots later than a plain line's, the same
    // kWindowRestartDots a mid-line activation costs. It is also where WX = 0
    // gets Pan Docs' "shifted left by SCX % 8 pixels": the match comes before
    // the line's first push, so the fine-scroll discard is still owed and is
    // spent on the window's own first tile. See docs/known-divergences.md,
    // "The window's X counter is compared once per dot, against a WX two dots
    // old", for the two references and the figures.
    if (windowX_ < kWindowCounterHeadStart) {
        discard_ += kWindowCounterHeadStart - windowX_;
    }
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
    if (!windowRendering_) {
        // Pan Docs: "after the window has started rendering". The window is on
        // but has not put a pixel into the queue yet, so there is nothing for
        // this to shift. See windowRendering_.
        return;
    }
    if (queueSize_ % 8 != 0) {
        // The FIFO's push port takes a push only when the pixel it is about to
        // hand over starts a row - measured as "only onto an empty FIFO", which
        // is what this is on every line whose fetcher has not taken the lead an
        // object fetch grants it (see fifoLead_): the FIFO is bare exactly then.
        // Once it is carrying the lead, the same dot is the one where nothing of
        // the previous row is left, and the queue holds whole rows only there.
        return;
    }
    // In front of the row, so the row's own pixels all move one right: Pan Docs'
    // pixel is an extra one, not a substitution.
    const int capacity = static_cast<int>(queue_.size());
    queueHead_ = (queueHead_ + capacity - 1) % capacity;
    queue_[static_cast<std::size_t>(queueHead_)] = 0;
    ++queueSize_;
}

void PixelPipeline::advanceWindowCounter() {
    // The counter's free increments, one per dot (kWindowCounterLeadDots). The
    // comparison against WX happens at the top of every dot, in stepDot, so
    // this only has to move the counter on: each of the values 0 to
    // kWindowCounterHeadStart is held for one dot and compared once, and on an
    // undisturbed line the last of them lands on the dot the first tile is
    // pushed.
    //
    // One per dot, not one per dot the fetcher runs: an object fetch triggered
    // at pixel 0 stalls the line's warm-up, and the increments carry on through
    // it. A Mealybug Tearoom reference measures that - it puts an object at
    // screen x = 0, which stalls eight dots before the fetcher's first step,
    // and its photograph still shows the window starting where the WX written
    // during mode 2 says. Holding the increments back with the fetcher instead
    // would let the mode-3 write overtake them. See
    // docs/known-divergences.md, "The window's X counter is compared once per
    // dot, against a WX two dots old".
    if (windowCounterLead_ > 0) {
        --windowCounterLead_;
        return;
    }
    if (windowXHeadStart_) {
        return; // from here the counter follows the pixels rendered
    }
    if (windowX_ == kWindowCounterHeadStart) {
        windowXHeadStart_ = true;
    } else {
        ++windowX_;
    }
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
    // Whether a pixel is due to leave the FIFO on this dot: one is waiting, or
    // the fetcher's row arrives before the dot is over. It is what an object
    // fetch needs to pre-empt (see the scan below), and it is read *here*, ahead
    // of the window's counter, because a window activation on this very dot
    // clears the FIFO and sends the fetcher back to its first step: the pixel was
    // due, and the fetch that pre-empts it begins whether or not the window then
    // takes the fetcher away. The alternative - the object waiting another
    // kWindowRestartDots for the window's own first row - is measured and wrong;
    // see docs/known-divergences.md, "A window fetch samples on the same dot a
    // background fetch does, and the object fetch that lands on the activation".
    const bool pixelDue = queueSize_ > 0 || fetchStallDots() == 0;
    // Pan Docs: "When this counter is equal to WX ... background rendering is
    // reset". An equality, tested on every dot the counter has started on, and
    // every match that finds the window not already drawing is an activation -
    // "this process can happen more than once per scanline". A counter that has
    // gone past WX is therefore not a match: a window enabled late, or a WX
    // lowered behind the counter, does not start on that line, and nothing on
    // the line after a stop restarts the window until WX is moved ahead of the
    // counter again.
    //
    // The counter is not compared for the first kWindowCounterLeadDots dots of
    // the line. Without that a WX of 0 would be matched on the line's very
    // first dot rather than by the counter's own first value, six dots in.
    if (windowCounterLead_ == 0 && windowEnabled(ppu) &&
        windowX_ == static_cast<int>(wxPipe_.front())) {
        if (!window_) {
            startWindow(ppu);
        } else if (windowX_ > windowComparedX_) {
            // The window is already drawing, so this is a WX that moved ahead
            // of the counter mid-window rather than a second activation: it
            // pushes one colour-0 pixel and leaves the window's row alone.
            pushWindowShiftPixel();
        }
    }
    // The comparator's kWindowCounterWxLag dots of pipeline move on whether or
    // not the rest of the line does: the lag is in dots, not in pixels. So do
    // the free increments themselves - see advanceWindowCounter.
    for (std::size_t i = 0; i + 1 < wxPipe_.size(); ++i) {
        wxPipe_[i] = wxPipe_[i + 1];
    }
    wxPipe_.back() = ppu.wx();
    advanceWindowCounter();
    // After the activation test, so that a line whose LCDC bit 5 is clear
    // throughout cannot start the window and stop it on the same dot, and
    // before the fetcher runs, so the tile it is working on when bit 5 goes
    // low is already a background tile.
    stopWindowIfDisabled(ppu);

    if (objectDots_ > 0) {
        --objectDots_;   // the fetch stalls the pixel stream
        if (objectLeadDots_ > 0) {
            // The dots of the line's first fetch the background fetcher keeps.
            // See kObjectFetcherLead.
            --objectLeadDots_;
            stepFetcher(ppu);
        }
        cancelObjectIfDisabled(ppu);
        if (objectFetching_ && objectDots_ < kObjectDataDots) {
            fetchObjectRow(ppu);
        }
        return false;
    }
    // Pan Docs, "Pixel FIFO", on what an object fetch begins with: "the fetcher
    // is advanced one step until it's at step 5 or until the background FIFO is
    // not empty". The fetch waits for a pixel to pre-empt, in other words, so an
    // object at screen x = 0 is fetched on the dot the line's first row reaches
    // the FIFO rather than on the line's first rendering dot, and the warm-up
    // that feeds that row is left alone. pixelDue, at the top of this function,
    // is that condition, read before the window could have taken the row away;
    // see docs/known-divergences.md, "An object fetch waits for the pixel it
    // pre-empts".
    if ((ppu.lcdc() & 0x02) != 0 && pixelDue) {
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
        queueHead_ = (queueHead_ + 1) % static_cast<int>(queue_.size());
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
