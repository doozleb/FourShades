#!/usr/bin/env python3
"""Build FourShades for the browser.

The core is plain C++20 with nothing but standard-library headers in it -- no
SDL, no OS calls, no files -- which is exactly why it compiles to WebAssembly
without a fight. This script compiles that core plus web/wasm_main.cpp and
drops the result next to the page.

    python web/build_wasm.py [--emsdk C:\\emsdk] [--out web/dist] [--debug]

Emscripten is not on PATH after an emsdk install; it lives inside the emsdk
directory and needs its environment set up first, so this finds em++ rather
than assuming a shell has been configured.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
WEB = ROOT / "web"

# The core, in the same order CMakeLists.txt lists it. Kept explicit rather
# than globbed for the same reason that file is: a source appearing in a build
# by accident is how a build stops meaning anything.
CORE_SOURCES = [
    "src/core/Cpu.cpp",
    "src/core/CpuLoads8.cpp",
    "src/core/CpuAlu8.cpp",
    "src/core/CpuWide.cpp",
    "src/core/CpuControl.cpp",
    "src/core/CpuCb.cpp",
    "src/core/Cartridge.cpp",
    "src/core/Timer.cpp",
    "src/core/Serial.cpp",
    "src/core/Joypad.cpp",
    "src/core/Ppu.cpp",
    "src/core/PixelPipeline.cpp",
    "src/core/GameBoy.cpp",
]

EXPORTED = [
    "_fs_rom_buffer", "_fs_load", "_fs_last_error", "_fs_loaded", "_fs_reset",
    "_fs_run_frame", "_fs_frame", "_fs_frame_width", "_fs_frame_height",
    "_fs_set_buttons", "_fs_set_sample_rate", "_fs_audio_available",
    "_fs_audio_data", "_fs_audio_clear", "_fs_audio_drop",
    "_fs_has_battery", "_fs_ram_size", "_fs_ram_data", "_fs_set_ram",
    "_malloc", "_free",
]


def discover_sources() -> list[str]:
    """Core sources, plus every .cpp under src/core subdirectories.

    The cartridge controllers and the audio channels live in src/core/mbc and
    src/core/apu and are added to CMakeLists.txt as they arrive; picking them
    up by directory here keeps the two from drifting apart silently.
    """
    sources = list(CORE_SOURCES)
    for sub in ("mbc", "apu"):
        d = ROOT / "src" / "core" / sub
        if d.is_dir():
            for cpp in sorted(d.glob("*.cpp")):
                sources.append(str(cpp.relative_to(ROOT)).replace("\\", "/"))
    sources.append("web/wasm_main.cpp")
    return sources


def find_empp(emsdk: pathlib.Path) -> pathlib.Path:
    """em++ inside an emsdk install, whichever version got activated."""
    direct = shutil.which("em++")
    if direct:
        return pathlib.Path(direct)
    upstream = emsdk / "upstream" / "emscripten"
    for name in ("em++.bat", "em++"):
        candidate = upstream / name
        if candidate.exists():
            return candidate
    raise SystemExit(
        f"em++ not found. Looked on PATH and in {upstream}.\n"
        f"Install it with:  cd {emsdk} && python emsdk.py install latest "
        f"&& python emsdk.py activate latest"
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--emsdk", default=r"C:\emsdk")
    ap.add_argument("--out", default=str(WEB / "dist"))
    ap.add_argument("--debug", action="store_true")
    args = ap.parse_args()

    empp = find_empp(pathlib.Path(args.emsdk))
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    sources = discover_sources()
    missing = [s for s in sources if not (ROOT / s).exists()]
    if missing:
        raise SystemExit("missing sources: " + ", ".join(missing))

    cmd = [str(empp)]
    cmd += sources
    cmd += [
        "-I", "src",
        "-std=c++20",
        "-O3" if not args.debug else "-O0",
        "-o", str(out / "fourshades-wasm.js"),
        # A module, not a page: the HTML is written by hand and version
        # controlled, so emscripten should not generate one over it.
        "-s", "MODULARIZE=0",
        "-s", "ENVIRONMENT=web",
        "-s", "ALLOW_MEMORY_GROWTH=1",
        # An 8 MiB cartridge plus the framebuffer plus the audio queue fits
        # inside this comfortably; growth covers anything larger.
        "-s", "INITIAL_MEMORY=33554432",
        "-s", "EXPORTED_FUNCTIONS=" + "[" + ",".join(f"'{e}'" for e in EXPORTED) + "]",
        "-s", "EXPORTED_RUNTIME_METHODS=['cwrap','HEAPU8','HEAPF32']",
        "-s", "FILESYSTEM=0",
        "-s", "EXIT_RUNTIME=0",
    ]
    if args.debug:
        cmd += ["-s", "ASSERTIONS=1", "-g"]

    print("building:", len(sources), "sources ->", out / "fourshades-wasm.js")
    result = subprocess.run(cmd, cwd=ROOT)
    if result.returncode != 0:
        return result.returncode

    # The page and its script sit beside the module, so the whole of `out` is
    # what gets deployed.
    for name in ("index.html", "fourshades.js"):
        shutil.copy2(WEB / name, out / name)

    total = sum(f.stat().st_size for f in out.iterdir() if f.is_file())
    print("\nbuilt into", out)
    for f in sorted(out.iterdir()):
        if f.is_file():
            print(f"  {f.name:28} {f.stat().st_size:>10,} bytes")
    print(f"  {'total':28} {total:>10,} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
