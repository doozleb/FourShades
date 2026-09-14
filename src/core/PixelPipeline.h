#pragma once

#include "core/Types.h"

#include <array>

namespace fourshades {

class Ppu;

// The two bits palette `palette` assigns to colour `colour` (Pan Docs
// "Palettes"): bits 1:0 for colour 0, 3:2 for 1, and so on.
u8 shadeFor(u8 palette, u8 colour);

// The hardware's rendering pipeline for one scanline. The fetcher's first
// three steps take two dots each and its push is retried every dot until the
// background queue is empty; meanwhile one pixel is emitted per dot.
class PixelPipeline {
public:
    // Called when mode 3 begins.
    void startLine(Ppu& ppu);
    // One dot of mode 3. Returns true once 160 pixels have been emitted.
    // Non-const: starting the window advances the PPU's window line counter.
    bool stepDot(Ppu& ppu, std::array<u8, 160>& line);

    int pixelX() const { return pixelX_; }

private:
    enum class Step { Tile, DataLow, DataHigh, Push };

    void stepFetcher(const Ppu& ppu);
    u16 tileRowAddress(const Ppu& ppu) const;

    Step step_ = Step::Tile;
    int stepDots_ = 0;   // dots spent in the current step
    int fetcherX_ = 0;   // tile column within the line
    bool discardFetch_ = true; // the line's first completed fetch is thrown away
    u8 tileIndex_ = 0;
    u8 tileLow_ = 0;
    u8 tileHigh_ = 0;
    std::array<u8, 8> queue_{}; // background colours waiting to be emitted
    int queueSize_ = 0;
    int queueHead_ = 0;
    int pixelX_ = 0;   // pixels emitted (0-160)
    int discard_ = 0;  // SCX % 8 pixels dropped at the start of the line
    bool window_ = false;        // drawing the window on this line
    bool windowCounted_ = false; // the window's line counter already advanced
    // The window's own line counter as it stood when the window started on
    // this line, cached so every fetch on the line reads the row the window
    // is actually drawing rather than the value left behind once
    // Ppu::advanceWindowLine() has bumped it for the next line.
    int windowLineUsed_ = 0;
};

} // namespace fourshades
