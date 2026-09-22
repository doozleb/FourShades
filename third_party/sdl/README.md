# Vendored SDL3

| Version | Source | SHA-256 (`SDL3-devel-3.4.16-VC.zip`) |
|---|---|---|
| 3.4.16 | https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16 (Windows VC development zip) | `1a784cb2a5c64d56fe7a62090fe9d242d9865f235e4ea9678f1a6ba4e693e7de` |

Unlike `third_party/doctest` and `third_party/nlohmann`, SDL3 is not a header
copied into this repository. It is a ~16 MB binary distribution (headers,
import libraries and DLLs for x86, x64 and arm64), so it is fetched on
demand and never committed, the same way `tools/sst/data/` and
`tools/roms/data/` are gitignored.

Run `python third_party/sdl/fetch_sdl.py` to download the zlib-licensed VC
archive, verify it against the SHA-256 above, and extract it into
`third_party/sdl/SDL3-3.4.16/`. A hash mismatch fails loudly and leaves
nothing extracted. Configuring the project without having run this script
fails loudly too, naming this script, rather than silently producing a
build with no window.

Only the x64 archive contents are wired into the build
(`third_party/sdl/SDL3-3.4.16/lib/x64/`), because `tools\dev.cmd` always
sets up the x64 developer environment.

## Refreshing to a new version

1. Pick the release from https://github.com/libsdl-org/SDL/releases and note
   its version number.
2. Download `SDL3-devel-<version>-VC.zip` and compute its SHA-256.
3. Update `VERSION` in `fetch_sdl.py`, and replace the line in
   `manifest.sha256` with the new hash and filename.
4. Update the version and hash in this README.
5. Delete any old extracted `third_party/sdl/SDL3-<old-version>/` tree, run
   `python third_party/sdl/fetch_sdl.py`, and reconfigure.
