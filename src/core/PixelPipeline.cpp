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
    windowCounted_ = false;
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
        if (!windowCounted_) {
            windowCounted_ = true;
            // Cache the row the window is drawing on this line before
            // advancing the PPU's counter for the next one: every fetch
            // below reads windowLineUsed_, never ppu.windowLine() directly,
            // so the just-bumped value doesn't leak into this line's tiles.
            windowLineUsed_ = ppu.windowLine();
            // The window's line counter only advances on lines that drew it.
            ppu.advanceWindowLine();
        }
    }
    stepFetcher(ppu);
    if (queueSize_ > 0) {
        const u8 colour = queue_[static_cast<std::size_t>(queueHead_)];
        queueHead_ = (queueHead_ + 1) % 8;
        --queueSize_;
        if (discard_ > 0) {
            --discard_;
        } else {
            const u8 shown = (ppu.lcdc() & 0x01) != 0 ? colour : 0;
            line[static_cast<std::size_t>(pixelX_)] = shadeFor(ppu.bgp(), shown);
            ++pixelX_;
        }
    }
    return pixelX_ >= 160;
}

} // namespace fourshades
