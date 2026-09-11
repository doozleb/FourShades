# FourShades: rules for agents

FourShades is a Game Boy emulator built in public. Its credibility is the
scoreboard, so these rules are not negotiable.

## Never

- Edit anything in `tools/sst/data/`, or `tools/sst/manifest.sha256`.
- Edit the README scoreboard block or `scoreboard.json` by hand. After a full
  run, use `python tools/scoreboard.py update build/sst-results.json`.
- Make code in `src/core/` depend on the tests: no test names, no file or JSON
  access, no includes from `tools/`. `tools/check_core_isolation.py` enforces it.
- Special-case a test, a test name, or an address pattern only a test uses.
- Loosen the comparator (`tools/sst/SstCompare.cpp`) or the loader to make
  something pass.

## When a test disagrees with the hardware documentation

Follow Pan Docs (https://gbdev.io/pandocs/). Leave the instruction failing,
and add an entry to `docs/known-divergences.md` with the evidence.

## Building (Windows, Visual Studio 2026)

`cmake` and `cl` are not on PATH. Run build commands through `tools\dev.cmd`:

    .\tools\dev.cmd cmake --preset release
    .\tools\dev.cmd cmake --build --preset release
    .\tools\dev.cmd ctest --preset release
    python tools/sst/fetch_sst.py
    .\build\release\tools\sst\sst_runner.exe
    python tools/scoreboard.py update build/sst-results.json

## Commits

Stage explicit paths. Never `git add -A` or `git add .`.
