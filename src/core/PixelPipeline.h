#pragma once

#include "core/Types.h"

#include <array>

namespace fourshades {

class Ppu;

// The two bits palette `palette` assigns to colour `colour` (Pan Docs
// "Palettes"): bits 1:0 for colour 0, 3:2 for 1, and so on.
u8 shadeFor(u8 palette, u8 colour);

// The hardware's rendering pipeline for one scanline. Pan Docs, "Pixel FIFO":
// the fetcher has five steps - Get tile, Get tile data low, Get tile data high,
// Sleep, Push - the first four of two dots each and the fifth attempted every
// dot until it succeeds; Get Tile Data High "also pushes a row of
// background/window pixels to the FIFO", which with the two Sleep dots makes
// "3 total chances to push pixels to the background FIFO every time the
// complete fetcher steps are performed". Meanwhile one pixel is emitted per
// dot. See stepFetcher for which of the three chances an undisturbed line uses
// and what pins that.
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
    // counted for ordinary background pixels - a fetch takes eight dots and
    // feeds eight pixels, so it never binds over the handful of dots at the
    // end of a line this is asked about - except across a window
    // activation: stepDot clears the queue and restarts the fetcher from
    // its first step when the window triggers, and WX can put that trigger
    // on the line's last pixels, so this counts kWindowRestartDots for an
    // activation still to come and, through fetchStallDots below, whatever
    // is left of one already running. See docs/known-divergences.md,
    // "Rendering runs seven dots behind the mode-3 window".
    //
    // What comes out of this, stated so it can be checked rather than read:
    // mode 3 lasts its 172-dot minimum, plus SCX % 8 for the fine-scroll
    // discard, plus kWindowRestartDots for every window activation on the
    // line, plus the object penalties. Those three lengthening terms are
    // independent and additive and nothing else is in them. The same entry
    // records which of them hardware measures - the window's is the one
    // nothing outside this repository does - and how the dot-exact figures are
    // pinned, since STAT only moves on whole M-cycles and no picture reflects
    // this at all.
    int dotsRemaining(const Ppu& ppu) const;

    // The dots a window activation forces the fetcher to spend before its
    // first pixel reaches the queue: startWindow resets the fetcher and clears
    // the queue when the window triggers, so the reset's own dot (see
    // fetchReset_) and then Tile+DataLow+DataHigh (2 dots each, see
    // stepFetcher) run once more, with the row going in at the end of the last
    // of them. One fetch and not two,
    // whether or not the line's own thrown-away first fetch had finished - see
    // startWindow - which is what makes this the cost of an activation
    // anywhere on the line, the free increments included. dotsRemaining charges
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

    // Where the free increments fall. Pan Docs gives their number but not
    // their timing, and they are taken one per dot: the counter holds each of
    // the kWindowCounterHeadStart + 1 values 0 to 7 for one dot and is
    // compared against WX at each of them.
    //
    // An undisturbed line pushes its first tile on the thirteenth dot of
    // rendering - the reset's own dot, a six-dot fetch that is thrown away, and
    // a six-dot fetch whose Get-Tile-Data-High step pushes the row as it
    // completes, which is therefore also the dot that tile's
    // first pixel leaves the FIFO (see stepFetcher). The last free increment
    // falls on that dot, which is what lines a WX of kWindowCounterHeadStart up
    // with screen x = 0, so
    // value 0 is compared on the sixth dot of rendering and this many dots
    // pass before the counter is compared at all. Measured: see
    // docs/known-divergences.md, "The window's X counter is compared once per
    // dot, against a WX two dots old".
    static constexpr int kWindowCounterLeadDots = 5;

    // The dots by which the value WX reaches the comparator lags the register.
    // A Mealybug Tearoom reference fixes it: it rewrites WX four dots before
    // the line's first pixel, and its photograph shows which counter values
    // were compared against the old value and which against the new one.
    // Modelled as a lag on WX rather than on the comparator's result, so that
    // the counter itself stays Pan Docs' counter, reading
    // kWindowCounterHeadStart on the dot the first pixel is drawn. Same entry
    // in docs/known-divergences.md.
    static constexpr int kWindowCounterWxLag = 2;

    // The dots of the line's first object fetch that the background fetcher
    // keeps. An object fetch stalls the line, and two hardware sources measure
    // that stall from opposite sides:
    //
    //   - a Mealybug Tearoom reference rewrites BGP at known dots on a line
    //     with one object and photographs the seams, which say which pixel was
    //     being drawn on each of those dots. Over eighteen different OAM X
    //     values it puts the line's pixels exactly Pan Docs' full OBJ penalty
    //     behind an object-free line - no rebate at all;
    //   - the hardware-verified object timing ROM measures mode 3's length and
    //     puts it three dots short of that same sum, once per scanline.
    //
    // Both hold at once if the line's first object fetch costs the background
    // fetcher three dots less than it costs the pixels: the fetcher runs on
    // through this many of the stall's dots (see objectLeadDots_), the FIFO
    // carries the pixels it gains (see fifoLead_), and mode 3 ends with the
    // fetcher rather than with the last pixel (see dotsRemaining). Once per
    // line, because that is what the timing ROM's 104 cases measure. See
    // docs/known-divergences.md, "An object fetch costs the pixels three dots
    // more than it costs the fetcher and mode 3".
    static constexpr int kObjectFetcherLead = 3;

    // The dots an object's own fetch costs, as against the wait in front of it.
    // Pan Docs' OBJ penalty algorithm gives an object's stall two independent
    // terms: the pixels of The Pixel's background tile still to its right, minus
    // two, which is the background fetcher finishing the tile it is in and is
    // charged once per tile; and this flat six for fetching the object's own
    // tile, charged for every object. So the object's own fetch does not begin
    // on the dot the pixel counter reaches the object - it begins once the wait
    // is over, this many dots before the end of the stall - and that is the dot
    // LCDC bit 1 is read on. See abandonObjectIfDisabled and
    // docs/known-divergences.md, "LCDC bit 1 is read on the dot an object fetch
    // begins, and a fetch that never begins charges only the wait".
    static constexpr int kObjectFetchDots = 6;

    // Dots from the dot an object fetch reads its row out of VRAM to the dot
    // the pixel it pre-empts is drawn. Pan Docs, "Pixel FIFO", ends the object
    // fetch with "the lower address for the row of pixels of the target object
    // tile is now retrieved and lengthens mode 3 by 1 dot. Once the address is
    // retrieved this is the last chance for object fetch cancel to occur.
    // Exiting object fetch lengthens mode 3 by 1 dot" - the read, then one more
    // dot, then the pixel. Everything the address is built from is read on that
    // dot and not before: the object's height (LCDC bit 2), its tile, its row.
    // Measured, not only counted: see docs/known-divergences.md, "An object
    // fetch reads its row two dots before the pixel it pre-empts".
    static constexpr int kObjectDataDots = 2;

    // The dots by which the LCDC bits that choose a pixel's colour lag the
    // register. Two of them are read when a pixel leaves the FIFO - bit 0,
    // which blanks the background and the window, and bit 1, which lets an
    // object cover them - and neither is read on the dot the pixel is shaded:
    // both are read the dot before it.
    //
    // It is measured as a *difference* rather than as an absolute dot, which
    // is what makes it independent of where the pixel stream itself is pinned.
    // A Mealybug Tearoom reference writes BGP during mode 3 and photographs the
    // seam, and the seam sits on the pixel drawn on the first dot after the
    // writing M-cycle: the palette is read on the pixel's own dot (see
    // Ppu::bgp and docs/known-divergences.md, "Palette writes short the old and
    // new values together for one dot"). Three more references write these two
    // LCDC bits during mode 3 at that same kind of dot, and all three put their
    // seams one pixel further right than the palette's - and all three move
    // further from their photographs, not closer, if the lag is made two dots.
    // So the colour a pixel carries is chosen one dot before the palette shades
    // it, whatever dot that turns out to be. See docs/known-divergences.md,
    // "The LCDC bits that choose a pixel's colour are read one dot before the
    // palette shades it".
    static constexpr int kLcdcSelectLag = 1;

private:
    enum class Step { Tile, DataLow, DataHigh, Sleep, Push };

    void stepFetcher(const Ppu& ppu);
    // The tile-index stage - Mealybug Tearoom's stage `B` - and every register
    // that goes into its address: SCX for the map column, SCY for the map row,
    // LCDC's map-select bit, and the fine-scroll discard that shares SCX's
    // sample. Called on the stage's first dot; see stepFetcher.
    void sampleTileIndex(const Ppu& ppu);
    // Offers the row the fetcher has just assembled to the background FIFO.
    // Pan Docs' "3 total chances" are three calls to this - one at the end of
    // Get Tile Data High and one on each Sleep dot - and then the Push step
    // calls it every dot until it succeeds. The FIFO has one push port and
    // takes a row only when it is empty, so which of the chances succeeds is
    // decided by the pixel stream, not by the fetcher: see stepFetcher.
    void tryPushRow();
    // Pan Docs' two hardware conditions on a counter match: the "Y condition"
    // and LCDC bit 5, both read live, on the dot the match is tested. What the
    // match then does depends on whether the window is already drawing.
    bool windowEnabled(const Ppu& ppu) const;
    // Every condition the window needs other than the X counter's match:
    // windowEnabled above, and that the window is not already drawing. All
    // three are hardware conditions; there is no once-per-line latch, because
    // none is needed - see window_.
    bool windowConditions(const Ppu& ppu) const;
    // Resets background rendering to the window's tilemap, as a counter match
    // does on hardware.
    void startWindow(Ppu& ppu);
    // What a counter match does instead when the window is *already* drawing,
    // which is the case a WX moved ahead of the counter mid-window reaches.
    // Pan Docs, "Pixel FIFO":
    //
    //   "When the value of WX changes after the window has started rendering
    //   and the new value of WX is reached again, a pixel with color value of 0
    //   and the lowest priority is pushed onto the background FIFO."
    //
    // Colour 0 rather than a shade: it goes through BGP at emission like any
    // other background pixel, and it is the lowest priority for free, because
    // an object beats a background colour of 0 whatever the object's own
    // priority flag says. And a *push*: the pixel is an extra one, so the rest
    // of the line moves a pixel right and its last pixel falls off the edge.
    // It costs no dots - the dot it is emitted on is the dot the fetcher's own
    // push was going to use, and the fetcher simply pushes a dot later.
    //
    // "onto the background FIFO" is taken at its word: the FIFO has one push
    // port and takes a push only when it is empty, exactly as the fetcher's
    // push does (see tryPushRow), so a match that lands part-way through a tile
    // is swallowed. That is measured, not assumed - see
    // docs/known-divergences.md, "A WX changed while the window is drawing
    // pushes one colour-0 pixel, and only onto an empty FIFO".
    //
    // "after the window has started rendering" is taken at its word too, and it
    // is not the same as "the window is on": a window that has just activated
    // has an empty FIFO and a fetcher six dots from its first push, and a WX
    // reached again in those dots pushes nothing. See fifoFed_.
    //
    // Pan Docs' "Window behavior" page pushes the same pixel for the other
    // reason a match can fail to reset background rendering:
    //
    //   "On monochrome systems, if the Window is disabled via LCDC, but the
    //   other conditions are met and it would have started rendering exactly on
    //   a BG tile boundary, then where it would have started rendering, a
    //   single pixel with ID 0 is inserted."
    //
    // Same push, same port, and "exactly on a BG tile boundary" is the port's
    // own condition restated: the FIFO takes the pixel only when the pixel it
    // is about to hand over starts a row. So this is called for every match
    // that is not an activation, whichever of the two reasons it is not one,
    // and the two sentences need one rule between them rather than two. See
    // docs/known-divergences.md, "A counter match that does not reset
    // background rendering pushes one colour-0 pixel".
    void pushColourZeroPixel();
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
    // emitted unchanged, and the fetch in flight is a window fetch until it
    // ends, whichever of its steps the write lands between - so this is called
    // from stepFetcher, on the dot a fetch completes, and what it decides is
    // whether the *next* tile the fetcher goes for is a window tile. That is
    // the first sentence, and both halves of it are measured: see the note on
    // the function. The queue is not cleared and the fetcher is not restarted,
    // so the switch costs no dots and no fresh SCX fine-scroll discard is
    // taken: that, plus the fetcher keeping its column counter (see
    // fetcherX_), is the second sentence.
    void stopWindowIfDisabled(const Ppu& ppu);
    // Moves the counter on by one of its free increments, or spends one of the
    // kWindowCounterLeadDots that come first. Called once per dot of mode 3,
    // including the dots an object fetch stalls: the free increments are dots,
    // not pixels. Measured - see the note on the function.
    void advanceWindowCounter();
    u16 tileRowAddress(const Ppu& ppu) const;
    // Dots the fetcher still owes before the dot its next row reaches the FIFO,
    // not counting that dot itself, so dotsRemaining can charge a stall the
    // queue cannot cover. Zero once the row is assembled, which is where an
    // ordinary line spends the one dot its queue is empty.
    int fetchStallDots() const;

    struct ObjectPixel {
        u8 colour = 0;   // 0 is transparent
        u8 palette = 0;  // 0 = OBP0, 1 = OBP1
        bool behind = false; // the object's priority flag
    };

    // Begins the fetch of line object `index`: charges its dots and leaves the
    // reads to fetchObjectRow, kObjectDataDots dots before the pixel the fetch
    // pre-empts. Nothing of the object is read here, which is what lets a
    // write that lands in between change it - or cancel the fetch outright.
    void startObject(const Ppu& ppu, std::size_t index);
    // The end of an object fetch: reads the object's row out of VRAM with the
    // height LCDC bit 2 gives now, and merges it into the pixels the queue is
    // about to emit. Not called at all if the fetch was cancelled.
    void fetchObjectRow(const Ppu& ppu);
    // Pan Docs' condition on starting an object fetch, read on the dot that
    // fetch begins rather than on the dot the object was triggered: the two are
    // kObjectFetchDots dots apart at the end of a wait (see that constant and
    // objectPenalty), and a bit 1 that is clear when the wait ends means the
    // fetch never begins at all. Nothing of the object has been read by then, so
    // there is nothing to skip; what there is instead is the rest of the stall,
    // which is the object's own fetch and is not spent. Measured, on two bands of
    // one reference whose waits are five dots and four - which is what tells this
    // dot apart from any fixed offset from the trigger. See
    // docs/known-divergences.md, "LCDC bit 1 is read on the dot an object fetch
    // begins, and a fetch that never begins charges only the wait".
    //
    // An object whose wait is zero - a second object in a background tile that
    // has already paid its term - begins its fetch on the dot it is triggered,
    // where stepDot already reads bit 1 before triggering anything. So this is
    // the same rule as that one, at a different dot, rather than a second rule.
    void abandonObjectIfDisabled(const Ppu& ppu);
    // Pan Docs, "Pixel FIFO": "Object fetching may be canceled if LCDC.1 is
    // disabled while the PPU is fetching an object from OAM", and the last
    // chance for that is the dot the row's address is retrieved on. A cancelled
    // fetch still costs every dot it was charged - Pan Docs has the cancel
    // lengthening mode 3 too - so only the merge is skipped. A Mealybug
    // Tearoom reference measures both halves of that: it clears bit 1 across
    // the middle of a fetch and sets it again before the pixel is drawn, so
    // the emission-time test cannot account for what its photograph shows. The
    // same reference measures the dots from the other side, on the bands whose
    // object is off the left edge: their fetches are cancelled on the very dot
    // their row would have been read and their pixels still pay the whole
    // penalty. A fetch that had not begun is abandoned instead and pays only the
    // wait; see abandonObjectIfDisabled.
    void cancelObjectIfDisabled(const Ppu& ppu);
    // The dots line object `index` costs, given the running state of the
    // per-line penalty memo. Takes that state by reference so dotsRemaining
    // can walk the objects still to come over its own copy of it without
    // touching the pipeline's.
    int objectPenalty(const Ppu& ppu, std::size_t index, int& lastTile,
                      bool& lastTileValid, bool& penaltyStarted) const;

    Step step_ = Step::Tile;
    int stepDots_ = 0;   // dots spent in the current step
    // The row this fetch assembled has reached the FIFO, so the chances that
    // are left - the Sleep dots and the Push step - have nothing to offer.
    // Cleared for each fetch when its high bitplane is read.
    bool pushed_ = false;
    // A fetcher reset - the line's start and every window activation - costs
    // the dot it lands on before Get Tile begins.
    //
    // That dot is not in Pan Docs; it is what the two hardware measurements
    // leave once the steps above are right, and the two agree on it. Mode 3's
    // hardware-verified 172-dot minimum and the Mealybug references' pixel 0 on
    // line dot 100 both say twelve dots pass between the start of rendering and
    // the line's first push (see kRenderLag and docs/known-divergences.md,
    // "Rendering runs seven dots behind the mode-3 window"). With the push
    // landing at the end of Get Tile Data High, the two warm-up fetches account
    // for six dots each and leave exactly one over. Independently, the window's
    // restart costs kWindowRestartDots = six dots from the activation to the
    // pre-empted pixel, which a bare reset to Get Tile would make five. One dot
    // charged to the reset itself satisfies both, and nothing else found does.
    bool fetchReset_ = false;
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
    u8 tileIndex_ = 0;
    u8 tileLow_ = 0;
    u8 tileHigh_ = 0;
    // Background colours waiting to be emitted. A row is eight of them, and
    // the extra kObjectFetcherLead are the pixels the fetcher gains at the
    // line's first object fetch: the hardware FIFO is sixteen pixels deep and
    // can carry them, so a push does not have to wait for the queue to be bare
    // once the fetcher is running ahead. tryPushRow is where that is decided.
    std::array<u8, 8 + kObjectFetcherLead> queue_{};
    int queueSize_ = 0;
    int queueHead_ = 0;
    int pixelX_ = 0;   // pixels emitted (0-160)
    // Pixels shifted out of the queue and thrown away before the line's first
    // pixel: SCX % 8 of them for the fine-scroll adjustment, and, once the
    // window has been started by one of the counter's free increments, the
    // kWindowCounterHeadStart - WX of its leftmost pixels that fall off the
    // left edge. One counter for both, because on the hardware they are the
    // same pixels: each costs a dot, and neither advances pixelX_ or the
    // window's X counter. See startWindow, and sampleTileIndex for where the
    // fine-scroll half of it is read.
    int discard_ = 0;
    // The fetcher is drawing the window right now. Set when the X counter
    // matches WX and cleared again by stopWindowIfDisabled - on the dot a fetch
    // ends, not the dot LCDC bit 5 goes low - when bit 5 has gone low part-way
    // along the line. It also carries the whole of the
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
    // WX is compared against, for equality, on every dot from
    // kWindowCounterLeadDots dots in: 0 at the top of the line, then the
    // free increments one per dot, then one per pixel rendered. Every match
    // that finds the window not already drawing activates it and advances the
    // window's row, so one line can start the window any number of times.
    int windowX_ = 0;
    bool windowXHeadStart_ = false; // all the free increments have been taken
    // Dots still to pass before the counter is compared at all; see
    // kWindowCounterLeadDots. Until it reaches zero the counter reads 0 and is
    // not compared, which is what keeps a WX of 0 from being matched on the
    // line's very first dot.
    int windowCounterLead_ = 0;
    // The comparator's view of WX, one entry per dot of kWindowCounterWxLag:
    // [0] is the value it compares against this dot and the last entry is the
    // register as of the previous dot. Shifted once per dot, whether or not the
    // line moves a pixel, because the lag is in dots.
    std::array<u8, kWindowCounterWxLag> wxPipe_{};
    // The colour-selection stage's view of LCDC, one entry per dot of
    // kLcdcSelectLag: [0] is what this dot's pixel is masked and muxed with,
    // and the last entry is the register as of the previous dot. Shifted once
    // per dot whether or not the line moves a pixel - the dots an object fetch
    // stalls included - because the lag is in dots, exactly as wxPipe_ above.
    std::array<u8, kLcdcSelectLag> lcdcSelectPipe_{};
    // Whether a pixel has left the queue on this line yet. kLcdcSelectLag is
    // the gap between one pixel's colour being chosen and the previous one
    // being shaded, so the line's first pixel has nothing to lag behind: it is
    // chosen on the dot it is shaded. Two of the three references above measure
    // that. Each puts one object off the left edge of every line, at an OAM X
    // that walks the stall's length band by band, and on the one band where the
    // stall ends exactly on the dot the write lands both photograph the line's
    // first pixel with the *new* bit rather than the old one. It is the first
    // pixel of the line and not the first after any stall: another band of one
    // of them puts its object mid-line and photographs the pixel that fetch
    // pre-empts with the old bit, which rules the wider reading out. Same entry
    // in docs/known-divergences.md.
    bool pixelStreamStarted_ = false;
    // A row has reached the FIFO since the fetcher was last reset - the line's
    // start or a window activation. It is what an inserted pixel needs: there
    // has to be a row for it to go in front of.
    //
    // It carries both of Pan Docs' colour-0 sentences at once. For the WX
    // changed "after the window has started rendering" it is exactly "the
    // window has started rendering", because the last reset was that
    // activation: the six dots between an activation and its first push are
    // not started rendering, and a WX reached again in them pushes nothing.
    // For the disabled window's pixel it is the background's own first row -
    // a WX of 0 is matched by the counter's first free increment and a WX of
    // kWindowCounterHeadStart by the dot the line's first row arrives, and at
    // the top of that dot the row is not there yet, so neither inserts
    // anything. Measured both ways round: scoping the flag to window pixels
    // instead loses the disabled window's pixel outright, and dropping it
    // inserts one into the front of three references' every line.
    bool fifoFed_ = false;
    // The highest counter value already compared against WX, or -1 if none has
    // been. The comparison runs every dot but the counter does not move every
    // dot: it stands still for the six dots an activation's fetcher restart
    // takes, for every dot an object fetch stalls, and for every pixel the
    // discard throws away. Pan
    // Docs' pixel FIFO sentence is about a WX the counter "is reached again",
    // so a value the counter is merely already sitting on is not one: this is
    // what tells the two apart. Only pushColourZeroPixel reads it, because a
    // repeated match is only a problem there - a second activation is already
    // ruled out by window_ - and without it every activation would be followed
    // by a colour-0 pixel from its own match, and a WX written to any value at
    // or below kWindowCounterHeadStart would push one for a counter value the
    // free increments had already been through.
    int windowComparedX_ = -1;
    // The window's own line counter as it stood when the window started on
    // this line, cached so every fetch on the line reads the row the window
    // is actually drawing rather than the value left behind once
    // Ppu::advanceWindowLine() has bumped it for the next line.
    int windowLineUsed_ = 0;

    std::array<ObjectPixel, 8> objects_{}; // pixels waiting, index 0 is next
    int objectDots_ = 0;      // dots of penalty still owed for a fetch
    // The object whose fetch is running, if any: its reads happen at the end of
    // the stall (see kObjectDataDots), so the fetch has to remember which
    // object it is for.
    std::size_t objectIndex_ = 0;
    bool objectFetching_ = false;
    // Dots of the stall the background fetcher still runs through; granted once
    // per line at the first object fetch. See kObjectFetcherLead.
    int objectLeadDots_ = 0;
    // Pixels the FIFO carries beyond a row, which is what the fetcher's lead is
    // held in once it has been taken. Zero until the line's first object fetch,
    // so a line without objects pushes exactly as it always did.
    int fifoLead_ = 0;
    unsigned drawn_ = 0;      // bitmask of line objects already fetched
    // Background tile that already paid its share. Tile numbers can be
    // negative (an object off the left edge), so the "none yet" case needs
    // its own flag rather than a sentinel value.
    int lastPenaltyTile_ = 0;
    bool lastPenaltyTileValid_ = false;
    bool objectPenaltyStarted_ = false; // an object has been fetched this line
};

} // namespace fourshades
