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
    // stall them, and the hardware-verified object timing ROM measures
    // objects at OAM X 160-167 doing exactly that. The fetcher itself is not
    // counted for ordinary background pixels - a fetch takes six dots and
    // feeds eight pixels, so it never binds over the handful of dots at the
    // end of a line this is asked about - except across a window
    // activation: stepDot clears the queue and restarts the fetcher from
    // its first step when the window triggers, and WX can put that trigger
    // on the line's last pixels, so this counts kWindowRestartDots for an
    // activation still to come and, through fetchStallDots below, whatever
    // is left of one already running. See docs/known-divergences.md,
    // "Rendering runs seven dots behind the mode-3 window".
    int dotsRemaining(const Ppu& ppu) const;

    // The dots a window activation forces the fetcher to spend before its
    // first pixel reaches the queue: stepDot resets the fetcher to its Tile
    // step and clears the queue when the window triggers, so
    // Tile+DataLow+DataHigh (2 dots each, see stepFetcher) run once more
    // with nothing pushed before Push can run again. dotsRemaining charges
    // exactly this much for an activation it sees coming but that has not
    // happened yet, so the two must be kept in agreement if stepDot's
    // restart cost ever changes.
    static constexpr int kWindowRestartDots = 6;

    // The free increments the window's scanline X counter takes before the
    // line's first pixel is rendered. Pan Docs, "Window behavior":
    //
    //   "the PPU maintains a counter, initialized to 0 at the beginning of
    //   each scanline. The counter is incremented for each pixel rendered;
    //   however, it also increments 7 times before the first pixel is
    //   actually rendered (this covers pixels discarded during the initial
    //   "fine scroll" adjustment). When this counter is equal to WX, if the
    //   Y condition is true and the Window enable bit is set in LCDC,
    //   background rendering is reset, beginning anew from the active row of
    //   the Window's tilemap. The coordinate of the active Window row is
    //   then incremented."
    //
    // So a WX of exactly this value lines the window's first pixel up with
    // screen x = 0, and a smaller one is matched during the free increments,
    // leaving the window's leftmost pixels off the screen.
    static constexpr int kWindowCounterHeadStart = 7;

private:
    enum class Step { Tile, DataLow, DataHigh, Push };

    void stepFetcher(const Ppu& ppu);
    // Every condition the window needs other than the X counter's match:
    // Pan Docs' "Y condition" and LCDC bit 5, both read live, and that the
    // window is not already drawing. All three are hardware conditions; there
    // is no once-per-line latch, because none is needed - see window_.
    bool windowConditions(const Ppu& ppu) const;
    // Resets background rendering to the window's tilemap, as a counter match
    // does on hardware.
    void startWindow(Ppu& ppu);
    // Hands the line back to the background if LCDC bit 5 has gone low while
    // the window was drawing. Mealybug Tearoom's PPU notes, quoted in
    // docs/known-divergences.md:
    //
    //   "WIN_EN can be disabled during mode 3. The disabling will take effect
    //   at the end of the current window tile being drawn. When the current
    //   window tile has finished being drawn, the PPU will start drawing
    //   background tiles again."
    //   "When the background resumes drawing it is on a tile boundary. The low
    //   3 bits of SCX have no effect."
    //
    // The pixels already in the queue are the window tile being drawn and are
    // emitted unchanged, so the disabling first shows in the tile after them -
    // the one the fetcher is working on when bit 5 goes low, which becomes a
    // background fetch wherever among its steps the write lands. The queue is
    // not cleared and the fetcher is not restarted, so the switch costs no
    // dots and no fresh SCX fine-scroll discard is taken: that, plus the
    // fetcher keeping its column counter (see fetcherX_), is the second
    // sentence.
    void stopWindowIfDisabled(const Ppu& ppu);
    // Takes the counter's kWindowCounterHeadStart free increments, testing it
    // against WX at each of them.
    void takeWindowHeadStart(Ppu& ppu);
    u16 tileRowAddress(const Ppu& ppu) const;
    // Dots the fetcher still owes before its next Push, so dotsRemaining can
    // charge a stall the queue cannot cover. Zero when Push is next, which
    // is where an ordinary line spends the one dot its queue is empty.
    int fetchStallDots() const;

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
    // The fetcher's tile column. It counts background tiles from the left of
    // the line, is reset to 0 when the window activates and then counts window
    // tiles, and keeps counting where it is when a cleared LCDC bit 5 hands the
    // line back to the background - the fetcher has one column counter, not
    // one per source. So a background tile fetched after the window has been
    // switched off is the column the window's count reached, not the column
    // that would have been there had the window never drawn: the resumed
    // background is tile-aligned to where the window stopped, which is the
    // shape Mealybug's "the low 3 bits of SCX have no effect" describes.
    int fetcherX_ = 0;
    bool discardFetch_ = true; // the line's first completed fetch is thrown away
    // Whether the fetch in progress read its tile index from the window's
    // tilemap, latched at the step that read it. A clear of LCDC bit 5 that
    // lands after that step leaves a window tile index being addressed with
    // the background's row - the same shape as the bitplane mixing Mealybug's
    // notes describe for TILE_SEL and SCY - and, more visibly, decides whether
    // the windowSkip_ clip below has a window tile to apply to.
    bool fetchWindow_ = false;
    u8 tileIndex_ = 0;
    u8 tileLow_ = 0;
    u8 tileHigh_ = 0;
    std::array<u8, 8> queue_{}; // background colours waiting to be emitted
    int queueSize_ = 0;
    int queueHead_ = 0;
    int pixelX_ = 0;   // pixels emitted (0-160)
    int discard_ = 0;  // SCX % 8 pixels dropped at the start of the line
    // The fetcher is drawing the window right now. Set when the X counter
    // matches WX and cleared again by stopWindowIfDisabled when LCDC bit 5
    // goes low part-way along the line. It also carries the whole of the
    // once-at-a-time rule, with no activation latch beside it: the counter
    // only counts up and the comparison is an equality, so an unchanged WX
    // can never be matched twice and a WX moved *behind* the counter can never
    // be matched at all. That is Mealybug's "setting WIN_EN again during mode 3
    // on the same scanline will have no effect unless WX has been updated to
    // set the window to activate on a pixel that hasn't been drawn yet" - it
    // falls out of the equality rather than needing a flag. What this flag
    // rules out is the one case the counter cannot: WX raised to a value still
    // ahead of the counter while the window is already drawing. Pan Docs' pixel
    // FIFO page says that case pushes a colour-0, lowest-priority pixel instead
    // of restarting the window, so a match there is not an activation.
    bool window_ = false;
    // The window's scanline X counter (kWindowCounterHeadStart). It is what
    // WX is compared against, for equality, on every dot: 0 at the top of the
    // line, then the free increments, then one per pixel rendered. Every match
    // that finds the window not already drawing activates it and advances the
    // window's row, so one line can start the window any number of times.
    int windowX_ = 0;
    bool windowXHeadStart_ = false; // the free increments have been taken
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
