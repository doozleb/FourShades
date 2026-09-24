// FourShades in a browser: the page half.
//
// Three things are worth knowing before reading it, all of them lessons the
// desktop front end paid for first.
//
// 1. The browser's 60 Hz is not the Game Boy's 59.7275 Hz. Piece 3b measured
//    what locking to a 60.000 Hz display does: +0.4565%, 7.9 cents sharp, and
//    201 surplus samples a second against a 44.1 kHz card. So this does not
//    run one emulated frame per requestAnimationFrame. It runs as many frames
//    as real elapsed time says are due, at the machine's own rate.
//
// 2. An emulated frame is not a bounded amount of work. The PPU completes no
//    frames while the LCD is off, and a program can leave it off for as long
//    as it likes -- through a boot logo, through every screen transition. The
//    C++ side caps a single call for that reason, and this side caps how many
//    calls it will make before giving the browser its thread back.
//
// 3. A backlog of audio is not drift, and correcting it gently takes minutes.
//    The desktop shipped with a half-second lag for exactly this reason: its
//    correction was one sample a frame, and a single LCD-off stretch could
//    hand it twenty thousand. Above a few frames this throws the excess away.

const WIDTH = 160;
const HEIGHT = 144;

// The Game Boy's real frame rate: 4194304 / 70224.
const FRAME_SECONDS = 70224 / 4194304;

// One call into the emulator runs at most this many M-cycles. A frame is
// 17556, so this is about four frames' worth -- enough that an LCD-off
// stretch makes progress, bounded enough that the tab stays responsive.
const MAX_CYCLES_PER_CALL = 70000;

// Never run more than this many frames of catch-up in one animation frame.
// A tab that has been in the background for a minute must not try to emulate
// a minute when it comes back; it should drop that time on the floor.
const MAX_CATCHUP_FRAMES = 4;

// Audio: how deep the queue may get before it stops being drift. Four frames,
// about 67 ms, the same bound and the same reasoning as the desktop's ceiling
// -- inside the ~125 ms at which sound lagging picture becomes detectable.
const AUDIO_CEILING_SECONDS = 4 * FRAME_SECONDS;
const AUDIO_TARGET_SECONDS = 2 * FRAME_SECONDS;

const PALETTES = {
  grey: [[0xe6, 0xe8, 0xeb], [0x8b, 0x92, 0x9c], [0x45, 0x4b, 0x56], [0x0d, 0x0f, 0x12]],
  green: [[0x9b, 0xbc, 0x0f], [0x8b, 0xac, 0x0f], [0x30, 0x62, 0x30], [0x0f, 0x38, 0x0f]],
};

const KEYS = {
  ArrowRight: 0x01, ArrowLeft: 0x02, ArrowUp: 0x04, ArrowDown: 0x08,
  KeyZ: 0x10, KeyX: 0x20, Backspace: 0x40, Enter: 0x80,
};

class FourShades {
  constructor(module, canvas, status) {
    this.m = module;
    this.canvas = canvas;
    this.status = status;
    this.ctx = canvas.getContext('2d', { alpha: false });
    this.image = this.ctx.createImageData(WIDTH, HEIGHT);
    this.palette = PALETTES.grey;
    this.buttons = 0;
    this.paused = false;
    this.muted = false;
    this.romName = null;
    this.lastTime = null;
    this.carry = 0;
    this.audio = null;
    this.playHead = 0;

    this.api = {
      romBuffer: module.cwrap('fs_rom_buffer', 'number', ['number']),
      load: module.cwrap('fs_load', 'number', []),
      lastError: module.cwrap('fs_last_error', 'string', []),
      loaded: module.cwrap('fs_loaded', 'number', []),
      reset: module.cwrap('fs_reset', 'number', []),
      runFrame: module.cwrap('fs_run_frame', 'number', ['number']),
      frame: module.cwrap('fs_frame', 'number', []),
      setButtons: module.cwrap('fs_set_buttons', null, ['number']),
      setSampleRate: module.cwrap('fs_set_sample_rate', null, ['number']),
      audioAvailable: module.cwrap('fs_audio_available', 'number', []),
      audioData: module.cwrap('fs_audio_data', 'number', []),
      audioClear: module.cwrap('fs_audio_clear', null, []),
      hasBattery: module.cwrap('fs_has_battery', 'number', []),
      ramSize: module.cwrap('fs_ram_size', 'number', []),
      ramData: module.cwrap('fs_ram_data', 'number', []),
      setRam: module.cwrap('fs_set_ram', 'number', ['number', 'number']),
    };

    this.drawBlank();
    this.bindInput();
  }

  // -- picture ------------------------------------------------------------

  drawBlank() {
    const d = this.image.data;
    const [r, g, b] = this.palette[0];
    for (let i = 0; i < WIDTH * HEIGHT; i++) {
      d[i * 4] = r; d[i * 4 + 1] = g; d[i * 4 + 2] = b; d[i * 4 + 3] = 255;
    }
    this.ctx.putImageData(this.image, 0, 0);
  }

  draw() {
    const ptr = this.api.frame();
    const shades = this.m.HEAPU8.subarray(ptr, ptr + WIDTH * HEIGHT);
    const d = this.image.data;
    for (let i = 0; i < WIDTH * HEIGHT; i++) {
      const c = this.palette[shades[i]];
      const o = i * 4;
      d[o] = c[0]; d[o + 1] = c[1]; d[o + 2] = c[2]; d[o + 3] = 255;
    }
    this.ctx.putImageData(this.image, 0, 0);
  }

  setPalette(name) {
    this.palette = PALETTES[name] || PALETTES.grey;
    if (!this.api.loaded()) this.drawBlank(); else this.draw();
  }

  // -- sound --------------------------------------------------------------

  // Started on a gesture, because every browser refuses to make noise before
  // one. Until then the emulator runs and its samples are thrown away, which
  // is the right way round: the picture should not wait for permission.
  startAudio() {
    if (this.audio) return;
    const Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) return;
    this.audio = new Ctx();
    this.gain = this.audio.createGain();
    this.gain.connect(this.audio.destination);
    this.api.setSampleRate(this.audio.sampleRate);
    this.playHead = this.audio.currentTime;
  }

  pumpAudio() {
    if (!this.audio || this.audio.state !== 'running') {
      this.api.audioClear();
      return;
    }
    const count = this.api.audioAvailable();
    if (count < 2) return;

    const frames = count >> 1;
    const ptr = this.api.audioData();
    const src = this.m.HEAPF32.subarray(ptr, ptr + frames * 2);

    const buffer = this.audio.createBuffer(2, frames, this.audio.sampleRate);
    const left = buffer.getChannelData(0);
    const right = buffer.getChannelData(1);
    const level = this.muted ? 0 : 1;
    for (let i = 0; i < frames; i++) {
      left[i] = src[i * 2] * level;
      right[i] = src[i * 2 + 1] * level;
    }
    this.api.audioClear();

    const now = this.audio.currentTime;

    // The backlog rule. Below the ceiling the queue is just latency and is
    // left alone; above it, the excess is thrown away in one go rather than
    // trimmed, because trimming is what takes minutes.
    if (this.playHead > now + AUDIO_CEILING_SECONDS) {
      this.playHead = now + AUDIO_TARGET_SECONDS;
    }
    // And if the queue has run dry the head is in the past: playing from
    // there would schedule sound that is already late.
    if (this.playHead < now) {
      this.playHead = now + AUDIO_TARGET_SECONDS;
    }

    const node = this.audio.createBufferSource();
    node.buffer = buffer;
    node.connect(this.gain);
    node.start(this.playHead);
    this.playHead += frames / this.audio.sampleRate;
  }

  // -- input --------------------------------------------------------------

  bindInput() {
    const set = (code, down) => {
      const bit = KEYS[code];
      if (bit === undefined) return false;
      this.buttons = down ? (this.buttons | bit) : (this.buttons & ~bit);
      this.api.setButtons(this.buttons);
      return true;
    };
    window.addEventListener('keydown', (e) => {
      if (e.repeat) return;
      if (set(e.code, true)) { e.preventDefault(); this.startAudio(); return; }
      if (e.code === 'Space') { e.preventDefault(); this.togglePause(); }
      else if (e.code === 'KeyR') { this.reset(); }
      else if (e.code === 'KeyP') { this.cyclePalette(); }
      else if (e.code === 'KeyM') { this.muted = !this.muted; this.say(this.muted ? 'muted' : 'unmuted'); }
    });
    window.addEventListener('keyup', (e) => { if (set(e.code, false)) e.preventDefault(); });

    // Touch: the same eight buttons, for a phone.
    for (const el of document.querySelectorAll('[data-button]')) {
      const bit = KEYS[el.dataset.button];
      const press = (down) => (e) => {
        e.preventDefault();
        this.startAudio();
        this.buttons = down ? (this.buttons | bit) : (this.buttons & ~bit);
        this.api.setButtons(this.buttons);
        el.classList.toggle('held', down);
      };
      el.addEventListener('touchstart', press(true), { passive: false });
      el.addEventListener('touchend', press(false), { passive: false });
      el.addEventListener('touchcancel', press(false), { passive: false });
      el.addEventListener('mousedown', press(true));
      el.addEventListener('mouseup', press(false));
      el.addEventListener('mouseleave', press(false));
    }
  }

  cyclePalette() {
    this.setPalette(this.palette === PALETTES.grey ? 'green' : 'grey');
  }

  togglePause() {
    if (!this.api.loaded()) return;
    this.paused = !this.paused;
    this.lastTime = null;
    this.say(this.paused ? 'paused' : this.romName);
  }

  // -- cartridge ----------------------------------------------------------

  load(bytes, name) {
    const ptr = this.api.romBuffer(bytes.length);
    if (!ptr) { this.say('out of memory'); return; }
    this.m.HEAPU8.set(bytes, ptr);
    if (!this.api.load()) {
      this.say(this.api.lastError() || 'could not load that ROM');
      this.drawBlank();
      return;
    }
    this.romName = name;
    this.paused = false;
    this.lastTime = null;
    this.restoreSave();
    this.startAudio();
    this.say(name);
  }

  reset() {
    if (!this.api.loaded()) return;
    this.persistSave();
    this.api.reset();
    this.restoreSave();
    this.paused = false;
    this.lastTime = null;
    this.say(this.romName);
  }

  saveKey() {
    return this.romName ? `fourshades:sav:${this.romName}` : null;
  }

  // Battery saves live in localStorage, keyed by the ROM's name. It is the
  // browser's equivalent of the .sav beside the ROM, and it has the same
  // rule: a save whose length does not match this cartridge is refused and
  // left alone rather than cropped, because it is somebody's only copy.
  persistSave() {
    if (!this.api.hasBattery()) return;
    const size = this.api.ramSize();
    if (!size) return;
    const ptr = this.api.ramData();
    if (!ptr) return;
    const bytes = this.m.HEAPU8.subarray(ptr, ptr + size);
    let s = '';
    for (let i = 0; i < size; i++) s += String.fromCharCode(bytes[i]);
    try { localStorage.setItem(this.saveKey(), btoa(s)); } catch (e) { /* full or blocked */ }
  }

  restoreSave() {
    if (!this.api.hasBattery()) return;
    const key = this.saveKey();
    if (!key) return;
    let raw = null;
    try { raw = localStorage.getItem(key); } catch (e) { return; }
    if (!raw) return;
    let s;
    try { s = atob(raw); } catch (e) { return; }
    const size = this.api.ramSize();
    if (s.length !== size) return;       // not this cartridge's save
    const buf = this.m._malloc(size);
    for (let i = 0; i < size; i++) this.m.HEAPU8[buf + i] = s.charCodeAt(i) & 0xff;
    this.api.setRam(buf, size);
    this.m._free(buf);
  }

  say(text) {
    if (this.status) this.status.textContent = text || '';
  }

  // -- the loop -----------------------------------------------------------

  tick(now) {
    if (this.api.loaded() && !this.paused) {
      if (this.lastTime === null) this.lastTime = now;
      let elapsed = (now - this.lastTime) / 1000 + this.carry;
      this.lastTime = now;

      // Frames due by the machine's own clock, not the display's.
      let due = Math.floor(elapsed / FRAME_SECONDS);
      if (due > MAX_CATCHUP_FRAMES) {
        due = MAX_CATCHUP_FRAMES;   // came back from a background tab
        elapsed = due * FRAME_SECONDS;
      }
      this.carry = elapsed - due * FRAME_SECONDS;

      for (let i = 0; i < due; i++) {
        // An LCD-off stretch can need several calls before a frame lands.
        let guard = 8;
        while (!this.api.runFrame(MAX_CYCLES_PER_CALL) && guard-- > 0) { /* keep going */ }
      }
      if (due > 0) { this.draw(); this.pumpAudio(); }
    }
    requestAnimationFrame((t) => this.tick(t));
  }

  start() {
    requestAnimationFrame((t) => this.tick(t));
    window.addEventListener('beforeunload', () => this.persistSave());
    setInterval(() => this.persistSave(), 10000);
  }
}

window.FourShades = FourShades;
window.FS_PALETTES = PALETTES;
