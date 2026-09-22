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
    void startLine(const Ppu& ppu);
    // One dot of mode 3. Returns true once 160 pixels have been emitted.
    // Non-const in the PPU: starting the window advances the PPU's window
    // line counter, which is the dot's only effect on the PPU.
    bool stepDot(Ppu& ppu, std::array<u8, 160>& line);

    int pixelX() const { return pixelX_; }

    // The dots that separate the pipeline from the mode-3 window it runs
    // inside: rendering starts this many dots after mode 3 begins, and the
    // last pixels of the line reach the LCD this many dots after mode 3
    // ends. See dotsRemaining below and docs/known-divergences.md,
    // "Rendering runs seven dots behind the mode-3 window".
    static constexpr int kRenderLag = 7;

    // How many more dots the line needs before its last pixel reaches the
    // LCD, read off the pipeline's state rather than simulated. Mode 3 ends
    // kRenderLag dots before that, and the count is not the pixel count
    // alone: the object fetches still owed over the pixels that are left
    // stall them, and intr_2_mode0_timing_sprites measures objects at OAM
    // X 160-167 doing exactly that. The fetcher itself is not counted - a
    // fetch takes six dots and feeds eight pixels, so it never binds over
    // the handful of dots at the end of a line this is asked about.
    int dotsRemaining(const Ppu& ppu) const;

private:
    enum class Step { Tile, DataLow, DataHigh, Push };

    void stepFetcher(const Ppu& ppu);
    u16 tileRowAddress(const Ppu& ppu) const;

    struct ObjectPixel {
        u8 colour = 0;   // 0 is transparent
        u8 palette = 0;  // 0 = OBP0, 1 = OBP1
        bool behind = false; // the object's priority flag
    };

    // Fetches line object `index` and merges it into the pixels in the queue.
    void startObject(const Ppu& ppu, std::size_t index);
    // The dots line object `index` costs, given the running state of the
    // per-line penalty memo. Takes that state by reference so dotsRemaining
    // can walk the objects still to come over its own copy of it without
    // touching the pipeline's.
    int objectPenalty(const Ppu& ppu, std::size_t index, int& lastTile,
                      bool& lastTileValid, bool& penaltyStarted) const;

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
    int windowSkip_ = 0;         // window pixels off the left edge, for WX < 7
    // The window's own line counter as it stood when the window started on
    // this line, cached so every fetch on the line reads the row the window
    // is actually drawing rather than the value left behind once
    // Ppu::advanceWindowLine() has bumped it for the next line.
    int windowLineUsed_ = 0;

    std::array<ObjectPixel, 8> objects_{}; // pixels waiting, index 0 is next
    int objectDots_ = 0;      // dots of penalty still owed for a fetch
    unsigned drawn_ = 0;      // bitmask of line objects already fetched
    // Background tile that already paid its share. Tile numbers can be
    // negative (an object off the left edge), so the "none yet" case needs
    // its own flag rather than a sentinel value.
    int lastPenaltyTile_ = 0;
    bool lastPenaltyTileValid_ = false;
    bool objectPenaltyStarted_ = false; // an object has been fetched this line
};

} // namespace fourshades
