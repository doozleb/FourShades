# FourShades piece 1: foundation and SM83 CPU. Implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A cycle-accurate SM83 CPU core that passes the SingleStepTests suite
(500 instruction files × 1,000 tests), with a hash-pinned test harness, a
generated scoreboard, and Windows CI that recomputes the score on every push.

**Architecture:** `fourshades_core` is a static library with no window, files
or JSON. The CPU reaches memory only through a `Bus` interface, where each
call is one M-cycle. The test harness (`tools/sst/`) implements `Bus` as a
recording bus, loads the JSON tests, runs one instruction per test, and
compares registers, memory and every cycle. `tools/scoreboard.py` turns the
results into the README scoreboard, and CI fails if the committed scoreboard
differs from a fresh run.

**Tech Stack:** C++20, MSVC 14.51 (Visual Studio 2026 Community), CMake 4.3
(presets, minimum 3.25) + Ninja, doctest 2.4.11, nlohmann/json 3.12.0,
Python 3.12 (standard library only), GitHub Actions `windows-latest`.

**Spec:** `docs/superpowers/specs/2026-09-11-foundation-cpu-design.md`

## Global Constraints

- Windows only. No Linux, no Clang, no cross-platform work.
- C++20 (`CMAKE_CXX_STANDARD 20`, extensions off). Build with CMake presets and Ninja, using MSVC.
- `cmake` and `cl` are **not** on PATH. Run every build command from PowerShell in `C:\GameMode` through `.\tools\dev.cmd` (created in Task 1), e.g. `.\tools\dev.cmd cmake --build --preset release`.
- Core code is in namespace `fourshades`; harness code in namespace `sst`.
- `src/core/` must never mention (whole word, case-insensitive) `sst`, `json`, `fopen`, `ifstream`, `ofstream`, `fstream` or `singlesteptests`, and must only include `core/...` headers or standard headers.
- Test data: SingleStepTests/sm83 commit `f9c30210245dd691661db39f5ace022c465ecc2f`, directory `v1/`, 500 files, 1,000 tests each. File names are `xx.json` and `cb xx.json` (with a space), lower-case hex.
- An instruction **passes** only when all 1,000 of its tests pass. Score = passing files / 500.
- Cycle comparison: read (`r-m`) and write (`-wm`) cycles must match kind, address and value. Idle (`---`) cycles match on kind only.
- Never hand-edit `tools/sst/data/`, `tools/sst/manifest.sha256`, `scoreboard.json` or the README scoreboard block.
- If a test disagrees with Pan Docs (https://gbdev.io/pandocs/), follow Pan Docs, leave the test failing, and record it in `docs/known-divergences.md`.
- Commits: stage explicit paths only (never `git add -A` or `git add .`), and end every message with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- The network on this machine drops connections intermittently. Retry a failed download before concluding anything.

## Facts about the test data (verified 2026-09-11 by scanning all 500 files)

- Test keys: `name`, `initial`, `final`, `cycles`. Nothing else.
- `initial` keys, always all present: `pc sp a b c d e f h l ime ie ram`.
- `final` keys: `pc sp a b c d e f h l ime ram`, plus `ei` in the EI (`fb`) tests only, where it is always `1`.
- `ie` is 0 or 1, is never in `final`, and is not the 0xFFFF register. The loader validates it, then ignores it (interrupt delivery is piece 2).
- `f` never has low-nibble bits set. `final.ram` only lists addresses that are in `initial.ram`.
- Each cycle is `[address, value, kind]` with kind `r-m`, `-wm` or `---`. There are no null cycles.
- Each test starts with PC on the opcode, and the first cycle is the opcode fetch.
- HALT (`76`) and STOP (`10`): pattern read, idle, idle, with final PC = initial PC + 1 and no other register change.
- Cycle patterns (R = read, W = write, i = idle) the code must produce:

| Pattern | Opcodes |
|---|---|
| R | 00, 40-7f (not 76) register forms, 80-bf register forms, 04/05/0c/0d… INC/DEC r, 07 0f 17 1f 27 2f 37 3f, e9, f3, fb |
| RR | LD r,n (not 36); LD r,(HL); ALU (HL); ALU n; 0a 1a 2a 3a; f2; CB register forms |
| RW | 02 12 22 32, 70-75 77, e2 |
| Ri | INC/DEC rr, ADD HL,rr, f9 |
| RRR | LD rr,nn, POP, f0, CB BIT n,(HL) |
| RRW | 34 35 36 e0 |
| RRi | 18, f8 |
| Rii | 10, 76 |
| RRRR | fa |
| RRRW | ea, CB RLC…SRL/RES/SET (HL) |
| RRRi | c3, c9, d9 |
| RRii | e8 |
| RiWW | PUSH, RST |
| RRRWW | 08 |
| RRRiWW | cd |
| RR / RRi | JR cc (not taken / taken) |
| RRR / RRRi | JP cc |
| Ri / RiRRi | RET cc |
| RRR / RRRiWW | CALL cc |

## File map

| File | Responsibility | Task |
|---|---|---|
| `.gitignore`, `.gitattributes` | build output and test data ignored; manifest kept LF | 1, 4 |
| `CMakeLists.txt`, `CMakePresets.json` | targets and debug/release presets | 1, 3, 4 |
| `tools/dev.cmd` | runs a command inside the MSVC developer environment | 1 |
| `third_party/doctest/doctest.h`, `third_party/nlohmann/json.hpp`, `third_party/README.md` | vendored headers and where they came from | 1 |
| `src/core/Types.h` | integer aliases, byte helpers | 1 |
| `src/core/Registers.h` | register file, F masked to its high nibble | 1 |
| `src/core/Bus.h` | the CPU's only view of the world | 2 |
| `src/core/Cpu.h`, `Cpu.cpp` | step, fetch, helpers, dispatch, NOP/HALT/STOP/EI/DI/illegal | 3 |
| `src/core/CpuLoads8.cpp` | 8-bit loads | 3 (stub), 9 |
| `src/core/CpuAlu8.cpp` | 8-bit arithmetic/logic, A-rotates, DAA/CPL/SCF/CCF | 3 (stub), 10 |
| `src/core/CpuWide.cpp` | 16-bit loads, stack, 16-bit arithmetic | 3 (stub), 11 |
| `src/core/CpuControl.cpp` | jumps, calls, returns, RST | 3 (stub), 12 |
| `src/core/CpuCb.cpp` | CB-prefixed instructions | 3 (stub), 13 |
| `tools/sst/SstTypes.h`, `RecordingBus.h` | cycle record; recording bus | 2 |
| `tools/sst/Sha256.h/.cpp` | SHA-256 for manifest checks | 4 |
| `tools/sst/Manifest.h/.cpp` | read/verify the manifest, read files | 4 |
| `tools/sst/Selection.h/.cpp` | `--only` opcode lists and ranges | 4 |
| `tools/sst/fetch_sst.py`, `manifest.sha256` | download pinned data; committed hashes | 4 |
| `tools/sst/SstLoader.h/.cpp` | strict JSON → `SstTest` | 5 |
| `tools/sst/SstCompare.h/.cpp` | expected vs actual → first mismatch | 5 |
| `tools/sst/SstRun.h/.cpp` | run one test on a fresh CPU | 5 |
| `tools/sst/sst_runner.cpp`, `tools/sst/CMakeLists.txt` | the runner executable; harness targets | 4, 5, 6 |
| `tools/scoreboard.py`, `tools/test_scoreboard.py` | results → README + scoreboard.json, with `check` | 7 |
| `tools/check_core_isolation.py` | structural check on `src/core` | 7 |
| `CLAUDE.md`, `docs/known-divergences.md`, `README.md`, `scoreboard.json` | agent rules, divergences, public scoreboard | 7, 14 |
| `.github/workflows/ci.yml` | Windows CI | 8 |
| `tests/*.cpp` | doctest unit tests (all globbed into one executable) | 1-5 |

The spec's architecture sketch names `CpuAlu.cpp` and `CpuCb.cpp`. This plan
splits the opcode groups one file per task (Loads8, Alu8, Wide, Control, Cb),
so each task owns exactly one file. The architecture is unchanged.

---

### Task 1: Project skeleton and registers

**Files:**
- Modify: `.gitignore`
- Delete: `site/` (untracked leftovers of the old website: only `dist/` and `node_modules/`)
- Create: `CMakeLists.txt`, `CMakePresets.json`, `tools/dev.cmd`
- Create: `third_party/doctest/doctest.h` (copy), `third_party/nlohmann/json.hpp` (download), `third_party/README.md`
- Create: `src/core/Types.h`, `src/core/Registers.h`
- Test: `tests/test_main.cpp`, `tests/test_registers.cpp`

**Interfaces:**
- Produces: `fourshades::u8`, `u16`, `i8`; `make16(u8 high, u8 low) -> u16`; `hi(u16) -> u8`; `lo(u16) -> u8`.
- Produces: `struct fourshades::Registers` with public `u8 a,b,c,d,e,h,l; u16 sp, pc;`, `u8 f() const`, `void setF(u8)`, `bool flag(u8 mask) const`, `af()/bc()/de()/hl()`, `setAf/setBc/setDe/setHl(u16)`, and constants `FlagZ=0x80, FlagN=0x40, FlagH=0x20, FlagC=0x10`.
- Produces: `tools\dev.cmd <command...>`, plus presets `debug`/`release` with build dirs `build/debug`, `build/release`.

- [ ] **Step 1: Clean up leftovers and replace `.gitignore`**

Confirm `site/` holds only generated output, then remove it:

```powershell
Get-ChildItem C:\GameMode\site | Select-Object -ExpandProperty Name   # expect: dist, node_modules
Remove-Item -Recurse -Force C:\GameMode\site -Confirm:$false
```

Replace the whole of `.gitignore` with:

```
build/
out/
.vs/
tools/sst/data/
__pycache__/
```

- [ ] **Step 2: Create the build files**

`tools/dev.cmd`:

```bat
@echo off
rem Runs one command inside the Visual Studio x64 developer environment, so
rem cmake, ninja and cl are on PATH.   Usage:  tools\dev.cmd cmake --preset release
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo error: vswhere.exe not found - is Visual Studio installed?
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo error: no Visual Studio installation with the C++ tools was found
    exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl >nul 2>&1 || (
    echo error: vcvars64.bat did not put cl on PATH
    exit /b 1
)
%*
exit /b %errorlevel%
```

`CMakePresets.json`:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": { "CMAKE_CXX_COMPILER": "cl" }
    },
    { "name": "debug", "inherits": "base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" } },
    { "name": "release", "inherits": "base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } }
  ],
  "buildPresets": [
    { "name": "debug", "configurePreset": "debug" },
    { "name": "release", "configurePreset": "release" }
  ],
  "testPresets": [
    { "name": "debug", "configurePreset": "debug", "output": { "outputOnFailure": true } },
    { "name": "release", "configurePreset": "release", "output": { "outputOnFailure": true } }
  ]
}
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.25)

project(FourShades VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # /Zc:__cplusplus makes __cplusplus report the real standard. /FS stops
    # parallel compiles racing on one .pdb (seen in Litharia as C1041).
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /FS)
endif()

enable_testing()

# ---------------------------------------------------------------------------
# Unit tests (doctest). Every tests/*.cpp file is part of this one executable.
# ---------------------------------------------------------------------------
file(GLOB FOURSHADES_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
add_executable(fourshades_tests ${FOURSHADES_TEST_SOURCES})
target_include_directories(fourshades_tests PRIVATE src third_party tools)
add_test(NAME unit COMMAND fourshades_tests)
```

- [ ] **Step 3: Vendor the two headers**

```powershell
New-Item -ItemType Directory -Force third_party\doctest, third_party\nlohmann | Out-Null
Copy-Item C:\Litharia\libs\doctest\doctest.h third_party\doctest\doctest.h
curl.exe -sL --retry 5 -o third_party\nlohmann\json.hpp https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
python -c "import hashlib;[print(hashlib.sha256(open(p,'rb').read()).hexdigest(), p) for p in ('third_party/doctest/doctest.h','third_party/nlohmann/json.hpp')]"
```

Expected output, exactly:

```
28846c518fc824eb37354bba40fb9a6372d67f891562d243b663b76190dc6bd7 third_party/doctest/doctest.h
aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63 third_party/nlohmann/json.hpp
```

If a hash differs, stop and report it. Don't continue with an unverified header.

`third_party/README.md`:

```markdown
# Vendored third-party code

| File | Version | Source | SHA-256 |
|---|---|---|---|
| `doctest/doctest.h` | 2.4.11 | https://github.com/doctest/doctest (copied from Litharia's `libs/doctest`) | `28846c518fc824eb37354bba40fb9a6372d67f891562d243b663b76190dc6bd7` |
| `nlohmann/json.hpp` | 3.12.0 | https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp | `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63` |

Both are MIT-licensed single headers. They are used only by the unit tests and
the test harness; the emulator core depends on neither.
```

- [ ] **Step 4: Write the failing tests**

`tests/test_main.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
```

`tests/test_registers.cpp`:

```cpp
#include <doctest/doctest.h>

#include "core/Registers.h"

using namespace fourshades;

TEST_CASE("F keeps its low nibble at zero whatever is written") {
    Registers r;
    r.setF(0xFF);
    CHECK(r.f() == 0xF0);
    r.setAf(0x12FF);
    CHECK(r.a == 0x12);
    CHECK(r.f() == 0xF0);
    CHECK(r.af() == 0x12F0);
}

TEST_CASE("register pairs read and write both halves") {
    Registers r;
    r.setBc(0x1234);
    CHECK(r.b == 0x12);
    CHECK(r.c == 0x34);
    CHECK(r.bc() == 0x1234);
    r.setDe(0xABCD);
    CHECK(r.de() == 0xABCD);
    r.setHl(0x0102);
    CHECK(r.h == 0x01);
    CHECK(r.l == 0x02);
    CHECK(r.hl() == 0x0102);
}

TEST_CASE("flag reads individual bits of F") {
    Registers r;
    r.setF(Registers::FlagZ | Registers::FlagC);
    CHECK(r.flag(Registers::FlagZ));
    CHECK_FALSE(r.flag(Registers::FlagN));
    CHECK_FALSE(r.flag(Registers::FlagH));
    CHECK(r.flag(Registers::FlagC));
}
```

- [ ] **Step 5: Run to verify it fails**

```powershell
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
```

Expected: the configure succeeds, then the build FAILS with `cannot open include file: 'core/Registers.h'`.

- [ ] **Step 6: Implement**

`src/core/Types.h`:

```cpp
#pragma once

#include <cstdint>

namespace fourshades {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using i8 = std::int8_t;

constexpr u16 make16(u8 high, u8 low) { return static_cast<u16>((high << 8) | low); }
constexpr u8 hi(u16 value) { return static_cast<u8>(value >> 8); }
constexpr u8 lo(u16 value) { return static_cast<u8>(value & 0xFF); }

} // namespace fourshades
```

`src/core/Registers.h`:

```cpp
#pragma once

#include "core/Types.h"

namespace fourshades {

// The SM83 register file. F is private because its low four bits don't exist
// on the real chip: they always read as zero, whatever is written.
struct Registers {
    static constexpr u8 FlagZ = 0x80;
    static constexpr u8 FlagN = 0x40;
    static constexpr u8 FlagH = 0x20;
    static constexpr u8 FlagC = 0x10;

    u8 a = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
    u16 sp = 0, pc = 0;

    u8 f() const { return f_; }
    void setF(u8 value) { f_ = static_cast<u8>(value & 0xF0); }
    bool flag(u8 mask) const { return (f_ & mask) != 0; }

    u16 af() const { return make16(a, f_); }
    u16 bc() const { return make16(b, c); }
    u16 de() const { return make16(d, e); }
    u16 hl() const { return make16(h, l); }
    void setAf(u16 value) { a = hi(value); setF(lo(value)); }
    void setBc(u16 value) { b = hi(value); c = lo(value); }
    void setDe(u16 value) { d = hi(value); e = lo(value); }
    void setHl(u16 value) { h = hi(value); l = lo(value); }

private:
    u8 f_ = 0;
};

} // namespace fourshades
```

- [ ] **Step 7: Run to verify it passes**

```powershell
.\tools\dev.cmd cmake --build --preset release
.\tools\dev.cmd ctest --preset release
```

Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 8: Commit**

```powershell
git add .gitignore CMakeLists.txt CMakePresets.json tools/dev.cmd third_party/README.md third_party/doctest/doctest.h third_party/nlohmann/json.hpp src/core/Types.h src/core/Registers.h tests/test_main.cpp tests/test_registers.cpp
git commit -m "build: CMake/Ninja/MSVC skeleton, vendored headers, register file" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: The Bus interface and the recording bus

**Files:**
- Create: `src/core/Bus.h`, `tools/sst/SstTypes.h`, `tools/sst/RecordingBus.h`
- Test: `tests/test_recording_bus.cpp`

**Interfaces:**
- Consumes: `fourshades::u8/u16` (Task 1).
- Produces: `class fourshades::Bus` with pure virtual `u8 read(u16)`, `void write(u16, u8)`, `void idle()`.
- Produces: `enum class sst::CycleKind { Read, Write, Idle }`; `struct sst::Cycle { u16 address; u8 value; CycleKind kind; }` with defaulted `==`.
- Produces: `class sst::RecordingBus final : public fourshades::Bus` with `poke(u16, u8)` (set memory, unlogged), `peek(u16) const -> u8`, `log() const -> const std::vector<Cycle>&`, `reset()`.

- [ ] **Step 1: Write the failing test**

`tests/test_recording_bus.cpp`:

```cpp
#include <doctest/doctest.h>

#include "sst/RecordingBus.h"

using sst::Cycle;
using sst::CycleKind;
using sst::RecordingBus;

TEST_CASE("reads, writes and idle cycles are logged in order") {
    RecordingBus bus;
    bus.poke(0x1234, 0xAB);
    CHECK(bus.log().empty());

    CHECK(bus.read(0x1234) == 0xAB);
    bus.write(0x2000, 0x55);
    bus.idle();

    REQUIRE(bus.log().size() == 3);
    CHECK(bus.log()[0] == Cycle{0x1234, 0xAB, CycleKind::Read});
    CHECK(bus.log()[1] == Cycle{0x2000, 0x55, CycleKind::Write});
    CHECK(bus.log()[2].kind == CycleKind::Idle);
    CHECK(bus.peek(0x2000) == 0x55);
}

TEST_CASE("reset clears touched memory and the log") {
    RecordingBus bus;
    bus.poke(0x0010, 1);
    bus.write(0x0020, 2);
    bus.reset();
    CHECK(bus.peek(0x0010) == 0);
    CHECK(bus.peek(0x0020) == 0);
    CHECK(bus.log().empty());
}
```

- [ ] **Step 2: Run to verify it fails**

```powershell
.\tools\dev.cmd cmake --build --preset release
```

Expected: FAIL with `cannot open include file: 'sst/RecordingBus.h'`.

- [ ] **Step 3: Implement**

`src/core/Bus.h`:

```cpp
#pragma once

#include "core/Types.h"

namespace fourshades {

// Everything the CPU can do to the outside world. Each call is exactly one
// M-cycle, which makes the CPU cycle-accurate by construction: it cannot
// spend a cycle the bus doesn't see.
class Bus {
public:
    virtual ~Bus() = default;
    virtual u8 read(u16 address) = 0;
    virtual void write(u16 address, u8 value) = 0;
    // A cycle with no memory access (16-bit arithmetic, a taken branch, ...).
    virtual void idle() = 0;
};

} // namespace fourshades
```

`tools/sst/SstTypes.h`:

```cpp
#pragma once

#include "core/Types.h"

namespace sst {

using fourshades::u16;
using fourshades::u8;

enum class CycleKind { Read, Write, Idle };

// One M-cycle of bus activity, as recorded by SingleStepTests or by
// RecordingBus.
struct Cycle {
    u16 address = 0;
    u8 value = 0;
    CycleKind kind = CycleKind::Idle;

    bool operator==(const Cycle&) const = default;
};

} // namespace sst
```

`tools/sst/RecordingBus.h`:

```cpp
#pragma once

#include "core/Bus.h"
#include "sst/SstTypes.h"

#include <vector>

namespace sst {

// A flat 64 KB memory that logs every cycle. One instance is reused across
// all 500,000 tests, so reset() only clears the addresses a test touched
// instead of zeroing 64 KB each time.
class RecordingBus final : public fourshades::Bus {
public:
    RecordingBus() : memory_(0x10000, 0) {}

    // Test setup: set memory without logging a cycle.
    void poke(u16 address, u8 value) {
        memory_[address] = value;
        touched_.push_back(address);
    }

    u8 peek(u16 address) const { return memory_[address]; }

    const std::vector<Cycle>& log() const { return log_; }

    void reset() {
        for (const u16 address : touched_) {
            memory_[address] = 0;
        }
        touched_.clear();
        log_.clear();
    }

    u8 read(u16 address) override {
        const u8 value = memory_[address];
        log_.push_back({address, value, CycleKind::Read});
        return value;
    }

    void write(u16 address, u8 value) override {
        poke(address, value);
        log_.push_back({address, value, CycleKind::Write});
    }

    // Address and value aren't modelled on idle cycles; the comparator checks
    // only their kind (see the design spec, "Match the test model").
    void idle() override { log_.push_back({0, 0, CycleKind::Idle}); }

private:
    std::vector<u8> memory_;
    std::vector<u16> touched_;
    std::vector<Cycle> log_;
};

} // namespace sst
```

- [ ] **Step 4: Run to verify it passes**

```powershell
.\tools\dev.cmd cmake --build --preset release
.\tools\dev.cmd ctest --preset release
```

Expected: `100% tests passed`.

- [ ] **Step 5: Commit**

```powershell
git add src/core/Bus.h tools/sst/SstTypes.h tools/sst/RecordingBus.h tests/test_recording_bus.cpp
git commit -m "feat: Bus interface and recording bus for the test harness" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: CPU skeleton: step, fetch, dispatch, NOP/HALT/STOP/EI/DI, illegal lock

**Files:**
- Create: `src/core/Cpu.h`, `src/core/Cpu.cpp`
- Create (stubs, replaced in Tasks 9-13): `src/core/CpuLoads8.cpp`, `src/core/CpuAlu8.cpp`, `src/core/CpuWide.cpp`, `src/core/CpuControl.cpp`, `src/core/CpuCb.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_cpu.cpp`

**Interfaces:**
- Consumes: `Bus`, `Registers` (Tasks 1-2); `RecordingBus` in tests.
- Produces: `class fourshades::Cpu` with `explicit Cpu(Bus&)`, `void step()`, `enum class State { Running, Halted, Stopped, Locked }`, `State state() const`, `bool imePending() const`, `bool unimplemented() const`, public members `Registers regs` and `bool ime`. The private helpers declared in `Cpu.h` below are what Tasks 9-13 implement. Their names and signatures are fixed here.
- Produces: CMake target `fourshades_core`.

- [ ] **Step 1: Write the failing test**

`tests/test_cpu.cpp`:

```cpp
#include <doctest/doctest.h>

#include "core/Cpu.h"
#include "sst/RecordingBus.h"

#include <initializer_list>

using namespace fourshades;
using sst::CycleKind;
using sst::RecordingBus;

namespace {
// Places `program` at 0x0100, where each test points PC.
void load(RecordingBus& bus, std::initializer_list<u8> program) {
    u16 address = 0x0100;
    for (const u8 byte : program) {
        bus.poke(address++, byte);
    }
}
} // namespace

TEST_CASE("NOP is one read cycle and advances PC") {
    RecordingBus bus;
    load(bus, {0x00});
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.step();
    CHECK(cpu.regs.pc == 0x0101);
    REQUIRE(bus.log().size() == 1);
    CHECK(bus.log()[0].kind == CycleKind::Read);
    CHECK(bus.log()[0].address == 0x0100);
}

TEST_CASE("EI enables interrupts only after the next instruction") {
    RecordingBus bus;
    load(bus, {0xFB, 0x00});
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.step();
    CHECK_FALSE(cpu.ime);
    CHECK(cpu.imePending());
    cpu.step();
    CHECK(cpu.ime);
    CHECK_FALSE(cpu.imePending());
}

TEST_CASE("DI straight after EI leaves interrupts disabled") {
    RecordingBus bus;
    load(bus, {0xFB, 0xF3, 0x00});
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.step();
    cpu.step();
    cpu.step();
    CHECK_FALSE(cpu.ime);
    CHECK_FALSE(cpu.imePending());
}

TEST_CASE("DI disables interrupts immediately") {
    RecordingBus bus;
    load(bus, {0xF3});
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.ime = true;
    cpu.step();
    CHECK_FALSE(cpu.ime);
}

TEST_CASE("HALT stops fetching; each later step is one idle cycle") {
    RecordingBus bus;
    load(bus, {0x76, 0x00});
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.step();
    CHECK(cpu.state() == Cpu::State::Halted);
    CHECK(cpu.regs.pc == 0x0101);
    cpu.step();
    REQUIRE(bus.log().size() == 2);
    CHECK(bus.log()[1].kind == CycleKind::Idle);
    CHECK(cpu.regs.pc == 0x0101);
}

TEST_CASE("an illegal opcode locks the CPU") {
    RecordingBus bus;
    load(bus, {0xD3});
    Cpu cpu(bus);
    cpu.regs.pc = 0x0100;
    cpu.step();
    CHECK(cpu.state() == Cpu::State::Locked);
    cpu.step();
    CHECK(cpu.regs.pc == 0x0101);
    CHECK(bus.log().back().kind == CycleKind::Idle);
}
```

- [ ] **Step 2: Run to verify it fails**

```powershell
.\tools\dev.cmd cmake --build --preset release
```

Expected: FAIL with `cannot open include file: 'core/Cpu.h'`.

- [ ] **Step 3: Implement**

`src/core/Cpu.h`:

```cpp
#pragma once

#include "core/Bus.h"
#include "core/Registers.h"
#include "core/Types.h"

namespace fourshades {

// The Game Boy's SM83 CPU. It reaches the outside world only through Bus,
// one M-cycle per call, so every cycle it spends is visible on the bus.
class Cpu {
public:
    enum class State { Running, Halted, Stopped, Locked };

    explicit Cpu(Bus& bus) : bus_(bus) {}

    // Runs one whole instruction, opcode fetch included. While halted,
    // stopped or locked it spends one idle M-cycle instead.
    void step();

    State state() const { return state_; }

    // True after EI, until the instruction after it has run (Pan Docs: EI).
    bool imePending() const { return imeDelay_ > 0; }

    // Set when step() met an opcode with no implementation yet. The test
    // runner reports those instructions as unimplemented rather than failing.
    bool unimplemented() const { return unimplemented_; }

    Registers regs;
    bool ime = false;

private:
    // Cpu.cpp: fetch, register and stack helpers, dispatch, misc opcodes.
    u8 fetch8();
    u16 fetch16();
    u8 readR8(int index);               // 0-7: B C D E H L (HL) A
    void writeR8(int index, u8 value);
    u16 readRp(int index) const;        // 0-3: BC DE HL SP
    void writeRp(int index, u16 value);
    u16 readRp2(int index) const;       // 0-3: BC DE HL AF
    void writeRp2(int index, u16 value);
    void push16(u16 value);
    u16 pop16();
    bool condition(int index) const;    // 0-3: NZ Z NC C
    void setFlags(bool z, bool n, bool h, bool c);
    void execute(u8 opcode);
    bool executeMisc(u8 opcode);

    // CpuLoads8.cpp
    bool executeLoads8(u8 opcode);

    // CpuAlu8.cpp
    bool executeAlu8(u8 opcode);
    void alu8(int operation, u8 value); // 0-7: ADD ADC SUB SBC AND XOR OR CP
    u8 inc8(u8 value);
    u8 dec8(u8 value);
    void daa();

    // CpuWide.cpp
    bool executeWide(u8 opcode);
    void addHl(u16 value);
    u16 addSpOffset(u8 offset);

    // CpuControl.cpp
    bool executeControl(u8 opcode);
    void jumpRelative(u8 offset);

    // CpuCb.cpp
    void executeCb();
    u8 rotateShift(int operation, u8 value); // 0-7: RLC RRC RL RR SLA SRA SWAP SRL

    Bus& bus_;
    State state_ = State::Running;
    int imeDelay_ = 0;
    bool unimplemented_ = false;
};

} // namespace fourshades
```

`src/core/Cpu.cpp`:

```cpp
#include "core/Cpu.h"

namespace fourshades {

void Cpu::step() {
    if (state_ != State::Running) {
        bus_.idle();
        return;
    }
    execute(fetch8());
    // EI sets the delay to 2, so IME turns on at the end of the instruction
    // after EI. DI zeroes it, cancelling a pending EI.
    if (imeDelay_ > 0 && --imeDelay_ == 0) {
        ime = true;
    }
}

u8 Cpu::fetch8() {
    const u8 value = bus_.read(regs.pc);
    regs.pc = static_cast<u16>(regs.pc + 1);
    return value;
}

u16 Cpu::fetch16() {
    const u8 low = fetch8();
    const u8 high = fetch8();
    return make16(high, low);
}

u8 Cpu::readR8(int index) {
    switch (index) {
    case 0: return regs.b;
    case 1: return regs.c;
    case 2: return regs.d;
    case 3: return regs.e;
    case 4: return regs.h;
    case 5: return regs.l;
    case 6: return bus_.read(regs.hl());
    default: return regs.a;
    }
}

void Cpu::writeR8(int index, u8 value) {
    switch (index) {
    case 0: regs.b = value; break;
    case 1: regs.c = value; break;
    case 2: regs.d = value; break;
    case 3: regs.e = value; break;
    case 4: regs.h = value; break;
    case 5: regs.l = value; break;
    case 6: bus_.write(regs.hl(), value); break;
    default: regs.a = value; break;
    }
}

u16 Cpu::readRp(int index) const {
    switch (index) {
    case 0: return regs.bc();
    case 1: return regs.de();
    case 2: return regs.hl();
    default: return regs.sp;
    }
}

void Cpu::writeRp(int index, u16 value) {
    switch (index) {
    case 0: regs.setBc(value); break;
    case 1: regs.setDe(value); break;
    case 2: regs.setHl(value); break;
    default: regs.sp = value; break;
    }
}

u16 Cpu::readRp2(int index) const {
    return index == 3 ? regs.af() : readRp(index);
}

void Cpu::writeRp2(int index, u16 value) {
    if (index == 3) {
        regs.setAf(value);
    } else {
        writeRp(index, value);
    }
}

void Cpu::push16(u16 value) {
    bus_.idle(); // SP is decremented before the first write, costing a cycle
    regs.sp = static_cast<u16>(regs.sp - 1);
    bus_.write(regs.sp, hi(value));
    regs.sp = static_cast<u16>(regs.sp - 1);
    bus_.write(regs.sp, lo(value));
}

u16 Cpu::pop16() {
    const u8 low = bus_.read(regs.sp);
    regs.sp = static_cast<u16>(regs.sp + 1);
    const u8 high = bus_.read(regs.sp);
    regs.sp = static_cast<u16>(regs.sp + 1);
    return make16(high, low);
}

bool Cpu::condition(int index) const {
    switch (index & 3) {
    case 0: return !regs.flag(Registers::FlagZ);
    case 1: return regs.flag(Registers::FlagZ);
    case 2: return !regs.flag(Registers::FlagC);
    default: return regs.flag(Registers::FlagC);
    }
}

void Cpu::setFlags(bool z, bool n, bool h, bool c) {
    regs.setF(static_cast<u8>((z ? Registers::FlagZ : 0) | (n ? Registers::FlagN : 0) |
                              (h ? Registers::FlagH : 0) | (c ? Registers::FlagC : 0)));
}

void Cpu::execute(u8 opcode) {
    if (executeMisc(opcode) || executeLoads8(opcode) || executeAlu8(opcode) ||
        executeWide(opcode) || executeControl(opcode)) {
        return;
    }
    unimplemented_ = true;
}

bool Cpu::executeMisc(u8 opcode) {
    switch (opcode) {
    case 0x00: // NOP
        return true;
    case 0x10: // STOP. Task 14 checks this against Pan Docs.
        state_ = State::Stopped;
        return true;
    case 0x76: // HALT
        state_ = State::Halted;
        return true;
    case 0xF3: // DI
        ime = false;
        imeDelay_ = 0;
        return true;
    case 0xFB: // EI
        imeDelay_ = 2;
        return true;
    case 0xCB:
        executeCb();
        return true;
    case 0xD3: case 0xDB: case 0xDD: case 0xE3: case 0xE4: case 0xEB:
    case 0xEC: case 0xED: case 0xF4: case 0xFC: case 0xFD:
        state_ = State::Locked; // illegal opcode: the real CPU hangs
        return true;
    default:
        return false;
    }
}

} // namespace fourshades
```

The five stub files. Each is replaced whole by a later task. `src/core/CpuLoads8.cpp`:

```cpp
#include "core/Cpu.h"

namespace fourshades {

bool Cpu::executeLoads8(u8) { return false; } // Task 9

} // namespace fourshades
```

`src/core/CpuAlu8.cpp`:

```cpp
#include "core/Cpu.h"

namespace fourshades {

bool Cpu::executeAlu8(u8) { return false; } // Task 10

} // namespace fourshades
```

`src/core/CpuWide.cpp`:

```cpp
#include "core/Cpu.h"

namespace fourshades {

bool Cpu::executeWide(u8) { return false; } // Task 11

} // namespace fourshades
```

`src/core/CpuControl.cpp`:

```cpp
#include "core/Cpu.h"

namespace fourshades {

bool Cpu::executeControl(u8) { return false; } // Task 12

} // namespace fourshades
```

`src/core/CpuCb.cpp`:

```cpp
#include "core/Cpu.h"

namespace fourshades {

// Task 13. Fetching the second byte keeps the cycle count honest meanwhile.
void Cpu::executeCb() {
    fetch8();
    unimplemented_ = true;
}

} // namespace fourshades
```

In `CMakeLists.txt`, insert this block directly above `enable_testing()`:

```cmake
# ---------------------------------------------------------------------------
# Emulator core. No window, no files, no test code: tools/check_core_isolation.py
# enforces that in CI.
# ---------------------------------------------------------------------------
add_library(fourshades_core STATIC
    src/core/Cpu.cpp
    src/core/CpuLoads8.cpp
    src/core/CpuAlu8.cpp
    src/core/CpuWide.cpp
    src/core/CpuControl.cpp
    src/core/CpuCb.cpp
)
target_include_directories(fourshades_core PUBLIC src)
```

and append this line at the end of the file:

```cmake
target_link_libraries(fourshades_tests PRIVATE fourshades_core)
```

- [ ] **Step 4: Run to verify it passes**

```powershell
.\tools\dev.cmd cmake --build --preset release
.\tools\dev.cmd ctest --preset release
```

Expected: `100% tests passed`.

- [ ] **Step 5: Commit**

```powershell
git add CMakeLists.txt src/core/Cpu.h src/core/Cpu.cpp src/core/CpuLoads8.cpp src/core/CpuAlu8.cpp src/core/CpuWide.cpp src/core/CpuControl.cpp src/core/CpuCb.cpp tests/test_cpu.cpp
git commit -m "feat: CPU skeleton with cycle-stepped fetch, EI delay, HALT/STOP and illegal-opcode lock" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: Pinned test data: SHA-256, manifest, selection, fetch script

**Files:**
- Create: `tools/sst/Sha256.h`, `tools/sst/Sha256.cpp`, `tools/sst/Manifest.h`, `tools/sst/Manifest.cpp`, `tools/sst/Selection.h`, `tools/sst/Selection.cpp`, `tools/sst/CMakeLists.txt`
- Create: `tools/sst/fetch_sst.py`, `tools/sst/manifest.sha256` (generated once in this task)
- Modify: `CMakeLists.txt`, `.gitattributes`
- Test: `tests/test_sha256.cpp`, `tests/test_manifest.cpp`, `tests/test_selection.cpp`

**Interfaces:**
- Produces: `std::string sst::sha256Hex(std::string_view data)`, plus class `sst::Sha256` (`update(const void*, size_t)`, `hexDigest()`).
- Produces: `struct sst::ManifestEntry { std::string sha256; std::string path; }` (path like `v1/cb 00.json`); `parseManifest(std::string_view) -> std::vector<ManifestEntry>` (throws `std::runtime_error`); `readBinaryFile(const std::filesystem::path&) -> std::string` (throws); `stemOf(const std::string& path) -> std::string` (`"v1/cb 00.json"` → `"cb 00"`); `verifyFiles(entries, dataDir, stems) -> std::vector<std::string>` (problems, empty = OK); `unexpectedFiles(entries, dataDir) -> std::vector<std::string>`.
- Produces: `sst::expandSelection(std::string_view spec, const std::vector<std::string>& available) -> std::vector<std::string>` (throws `std::runtime_error`).
- Produces: CMake target `sst_harness` (static, links `fourshades_core` publicly, and exposes `tools/` and `third_party/` include dirs).

- [ ] **Step 1: Write the failing tests**

`tests/test_sha256.cpp`:

```cpp
#include <doctest/doctest.h>

#include "sst/Sha256.h"

#include <string>

TEST_CASE("SHA-256 matches the FIPS 180-4 test vectors") {
    CHECK(sst::sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sst::sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sst::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(sst::sha256Hex(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA-256 gives the same answer fed one byte at a time") {
    const std::string text = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sst::Sha256 hash;
    for (const char c : text) {
        hash.update(&c, 1);
    }
    CHECK(hash.hexDigest() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}
```

`tests/test_manifest.cpp`:

```cpp
#include <doctest/doctest.h>

#include "sst/Manifest.h"
#include "sst/Sha256.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fsys = std::filesystem;

namespace {
const std::string kAbcHash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

fsys::path freshDir() {
    const fsys::path dir = fsys::temp_directory_path() / "fourshades-manifest-test";
    fsys::remove_all(dir);
    fsys::create_directories(dir / "v1");
    return dir;
}

void writeFile(const fsys::path& path, const std::string& content) {
    std::ofstream(path, std::ios::binary) << content;
}
} // namespace

TEST_CASE("parseManifest reads sha256sum lines, including names with spaces and CRLF") {
    const std::string text = kAbcHash + "  v1/00.json\r\n" + kAbcHash + "  v1/cb 00.json\n\n";
    const auto entries = sst::parseManifest(text);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].sha256 == kAbcHash);
    CHECK(entries[0].path == "v1/00.json");
    CHECK(entries[1].path == "v1/cb 00.json");
    CHECK(sst::stemOf(entries[1].path) == "cb 00");
}

TEST_CASE("parseManifest rejects malformed lines") {
    CHECK_THROWS_AS(sst::parseManifest("not a hash  v1/00.json\n"), std::runtime_error);
    CHECK_THROWS_AS(sst::parseManifest(kAbcHash + " v1/00.json\n"), std::runtime_error);
}

TEST_CASE("verifyFiles accepts matching files and reports changed or missing ones") {
    const fsys::path dir = freshDir();
    writeFile(dir / "v1" / "00.json", "abc");
    const std::vector<sst::ManifestEntry> entries{{kAbcHash, "v1/00.json"}, {kAbcHash, "v1/01.json"}};

    CHECK(sst::verifyFiles(entries, dir, {"00"}).empty());

    const auto missing = sst::verifyFiles(entries, dir, {"00", "01"});
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].find("01.json") != std::string::npos);

    writeFile(dir / "v1" / "00.json", "abd");
    CHECK(sst::verifyFiles(entries, dir, {"00"}).size() == 1);
}

TEST_CASE("unexpectedFiles reports files the manifest doesn't list") {
    const fsys::path dir = freshDir();
    writeFile(dir / "v1" / "00.json", "abc");
    writeFile(dir / "v1" / "zz.json", "extra");
    const std::vector<sst::ManifestEntry> entries{{kAbcHash, "v1/00.json"}};
    const auto extra = sst::unexpectedFiles(entries, dir);
    REQUIRE(extra.size() == 1);
    CHECK(extra[0].find("zz.json") != std::string::npos);
}
```

`tests/test_selection.cpp`:

```cpp
#include <doctest/doctest.h>

#include "sst/Selection.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace {
const std::vector<std::string> kAvailable{"00", "01", "40", "41", "42", "cb 00", "cb 01"};
}

TEST_CASE("ranges expand to the files that exist, in order") {
    CHECK(sst::expandSelection("40-42", kAvailable) == std::vector<std::string>{"40", "41", "42"});
    CHECK(sst::expandSelection("00-41", kAvailable) == std::vector<std::string>{"00", "01", "40", "41"});
    CHECK(sst::expandSelection("cb00-cb01", kAvailable) == std::vector<std::string>{"cb 00", "cb 01"});
}

TEST_CASE("single items accept 'cbXX' and 'cb XX', and duplicates are dropped") {
    CHECK(sst::expandSelection("00, cb 01,cb01,00", kAvailable) == std::vector<std::string>{"00", "cb 01"});
    CHECK(sst::expandSelection("40,41", kAvailable) == std::vector<std::string>{"40", "41"});
}

TEST_CASE("bad selections throw") {
    CHECK_THROWS_AS(sst::expandSelection("d3", kAvailable), std::runtime_error);     // no such file
    CHECK_THROWS_AS(sst::expandSelection("40-cb01", kAvailable), std::runtime_error); // mixed range
    CHECK_THROWS_AS(sst::expandSelection("42-40", kAvailable), std::runtime_error);   // backwards
    CHECK_THROWS_AS(sst::expandSelection("4", kAvailable), std::runtime_error);       // not two hex digits
}
```

- [ ] **Step 2: Run to verify it fails**

```powershell
.\tools\dev.cmd cmake --build --preset release
```

Expected: FAIL with `cannot open include file: 'sst/Sha256.h'`.

- [ ] **Step 3: Implement the C++ pieces**

`tools/sst/Sha256.h`:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sst {

// Minimal SHA-256 (FIPS 180-4). Used only to check the test data against the
// committed manifest, so the score is always measured on the real tests.
class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    std::string hexDigest(); // finalises; call once

private:
    void compress(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t bufferSize_ = 0;
    std::uint64_t totalBytes_ = 0;
};

std::string sha256Hex(std::string_view data);

} // namespace sst
```

`tools/sst/Sha256.cpp`:

```cpp
#include "sst/Sha256.h"

#include <algorithm>
#include <cstring>

namespace sst {

namespace {

constexpr std::uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

} // namespace

Sha256::Sha256()
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
             0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::update(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    totalBytes_ += size;
    while (size > 0) {
        const std::size_t take = std::min(size, buffer_.size() - bufferSize_);
        std::memcpy(buffer_.data() + bufferSize_, bytes, take);
        bufferSize_ += take;
        bytes += take;
        size -= take;
        if (bufferSize_ == buffer_.size()) {
            compress(buffer_.data());
            bufferSize_ = 0;
        }
    }
}

std::string Sha256::hexDigest() {
    const std::uint64_t bitLength = totalBytes_ * 8;
    const std::uint8_t marker = 0x80;
    update(&marker, 1);
    const std::uint8_t zero = 0;
    while (bufferSize_ != 56) {
        update(&zero, 1);
    }
    std::uint8_t length[8];
    for (int i = 0; i < 8; ++i) {
        length[i] = static_cast<std::uint8_t>(bitLength >> (56 - 8 * i));
    }
    update(length, 8);

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (const std::uint32_t word : state_) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out.push_back(kHex[(word >> shift) & 0xF]);
        }
    }
    return out;
}

void Sha256::compress(const std::uint8_t* block) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (std::uint32_t{block[4 * i]} << 24) | (std::uint32_t{block[4 * i + 1]} << 16) |
               (std::uint32_t{block[4 * i + 2]} << 8) | std::uint32_t{block[4 * i + 3]};
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + choose + kRound[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

std::string sha256Hex(std::string_view data) {
    Sha256 hash;
    hash.update(data.data(), data.size());
    return hash.hexDigest();
}

} // namespace sst
```

`tools/sst/Manifest.h`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sst {

// One line of tools/sst/manifest.sha256: "<sha256>  v1/<name>.json".
struct ManifestEntry {
    std::string sha256;
    std::string path;
};

std::vector<ManifestEntry> parseManifest(std::string_view text);
std::string readBinaryFile(const std::filesystem::path& path);

// "v1/cb 00.json" -> "cb 00"
std::string stemOf(const std::string& path);

// One message per selected file that is missing or whose hash differs.
// Empty means every selected file matches the manifest.
std::vector<std::string> verifyFiles(const std::vector<ManifestEntry>& entries,
                                     const std::filesystem::path& dataDir,
                                     const std::vector<std::string>& stems);

// One message per file in <dataDir>/v1 that the manifest doesn't list.
std::vector<std::string> unexpectedFiles(const std::vector<ManifestEntry>& entries,
                                         const std::filesystem::path& dataDir);

} // namespace sst
```

`tools/sst/Manifest.cpp`:

```cpp
#include "sst/Manifest.h"

#include "sst/Sha256.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace sst {

std::vector<ManifestEntry> parseManifest(std::string_view text) {
    std::vector<ManifestEntry> entries;
    std::size_t lineNumber = 0;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            continue;
        }
        const bool hexDigest =
            line.size() > 66 && std::all_of(line.begin(), line.begin() + 64, [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            });
        if (!hexDigest || line.substr(64, 2) != "  ") {
            throw std::runtime_error("manifest line " + std::to_string(lineNumber) + " is malformed");
        }
        entries.push_back({std::string(line.substr(0, 64)), std::string(line.substr(66))});
    }
    return entries;
}

std::string readBinaryFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0);
    std::string data(size, '\0');
    in.read(data.data(), static_cast<std::streamsize>(size));
    return data;
}

std::string stemOf(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

std::vector<std::string> verifyFiles(const std::vector<ManifestEntry>& entries,
                                     const std::filesystem::path& dataDir,
                                     const std::vector<std::string>& stems) {
    std::vector<std::string> problems;
    for (const std::string& stem : stems) {
        const auto entry = std::find_if(entries.begin(), entries.end(),
                                        [&](const ManifestEntry& e) { return stemOf(e.path) == stem; });
        if (entry == entries.end()) {
            problems.push_back(stem + ": not in the manifest");
            continue;
        }
        const std::filesystem::path file = dataDir / std::filesystem::path(entry->path);
        if (!std::filesystem::exists(file)) {
            problems.push_back(entry->path + ": missing");
            continue;
        }
        if (sha256Hex(readBinaryFile(file)) != entry->sha256) {
            problems.push_back(entry->path + ": hash does not match the manifest");
        }
    }
    return problems;
}

std::vector<std::string> unexpectedFiles(const std::vector<ManifestEntry>& entries,
                                         const std::filesystem::path& dataDir) {
    std::set<std::string> known;
    for (const ManifestEntry& entry : entries) {
        known.insert(std::filesystem::path(entry.path).filename().string());
    }
    const std::filesystem::path v1 = dataDir / "v1";
    if (!std::filesystem::exists(v1)) {
        return {"v1: missing"};
    }
    std::vector<std::string> problems;
    for (const auto& item : std::filesystem::directory_iterator(v1)) {
        const std::string name = item.path().filename().string();
        if (!known.contains(name)) {
            problems.push_back("v1/" + name + ": not in the manifest");
        }
    }
    return problems;
}

} // namespace sst
```

`tools/sst/Selection.h`:

```cpp
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sst {

// Expands a --only list such as "40-7f,06,cb00-cbff" into test-file stems
// ("40" ... "7f", "06", "cb 00" ... "cb ff"). Ranges skip opcodes with no
// test file (illegal opcodes); a single item with no file is an error.
std::vector<std::string> expandSelection(std::string_view spec, const std::vector<std::string>& available);

} // namespace sst
```

`tools/sst/Selection.cpp`:

```cpp
#include "sst/Selection.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace sst {

namespace {

struct Item {
    bool cb = false;
    int value = 0;
};

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && text[begin] == ' ') ++begin;
    while (end > begin && text[end - 1] == ' ') --end;
    return std::string(text.substr(begin, end - begin));
}

Item parseItem(const std::string& original) {
    std::string text = original;
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    Item item;
    if (text.size() > 2 && text.rfind("cb", 0) == 0) {
        item.cb = true;
        text = trim(text.substr(2));
    }
    if (text.size() != 2 || !std::isxdigit(static_cast<unsigned char>(text[0])) ||
        !std::isxdigit(static_cast<unsigned char>(text[1]))) {
        throw std::runtime_error("bad opcode in --only: '" + original + "'");
    }
    item.value = std::stoi(text, nullptr, 16);
    return item;
}

std::string stem(Item item) {
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "%s%02x", item.cb ? "cb " : "", item.value);
    return buffer;
}

} // namespace

std::vector<std::string> expandSelection(std::string_view spec, const std::vector<std::string>& available) {
    const auto exists = [&](const std::string& name) {
        return std::find(available.begin(), available.end(), name) != available.end();
    };
    std::vector<std::string> out;
    const auto add = [&](const std::string& name) {
        if (std::find(out.begin(), out.end(), name) == out.end()) {
            out.push_back(name);
        }
    };

    std::size_t start = 0;
    while (start <= spec.size()) {
        const std::size_t comma = spec.find(',', start);
        const std::string token =
            trim(spec.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        start = comma == std::string_view::npos ? spec.size() + 1 : comma + 1;
        if (token.empty()) {
            continue;
        }
        const std::size_t dash = token.find('-');
        if (dash == std::string::npos) {
            const std::string name = stem(parseItem(token));
            if (!exists(name)) {
                throw std::runtime_error("no test file for '" + token + "'");
            }
            add(name);
            continue;
        }
        const Item first = parseItem(trim(token.substr(0, dash)));
        const Item last = parseItem(trim(token.substr(dash + 1)));
        if (first.cb != last.cb || first.value > last.value) {
            throw std::runtime_error("bad range in --only: '" + token + "'");
        }
        for (int value = first.value; value <= last.value; ++value) {
            const std::string name = stem({first.cb, value});
            if (exists(name)) {
                add(name);
            }
        }
    }
    return out;
}

} // namespace sst
```

`tools/sst/CMakeLists.txt`:

```cmake
# The SingleStepTests harness. Never linked into the emulator itself.
add_library(sst_harness STATIC
    Sha256.cpp
    Manifest.cpp
    Selection.cpp
)
target_include_directories(sst_harness PUBLIC ${CMAKE_SOURCE_DIR}/tools ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(sst_harness PUBLIC fourshades_core)
```

In the root `CMakeLists.txt`, add this line directly after `target_include_directories(fourshades_core PUBLIC src)`:

```cmake
add_subdirectory(tools/sst)
```

and change the final line from `target_link_libraries(fourshades_tests PRIVATE fourshades_core)` to:

```cmake
target_link_libraries(fourshades_tests PRIVATE sst_harness)
```

- [ ] **Step 4: Run to verify the C++ tests pass**

```powershell
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
.\tools\dev.cmd ctest --preset release
```

Expected: `100% tests passed`.

- [ ] **Step 5: Write the fetch script**

`tools/sst/fetch_sst.py`:

```python
"""
Download the SingleStepTests SM83 suite at the pinned commit and check every
file against the committed manifest.

    python tools/sst/fetch_sst.py                    # fetch, or re-verify what's there
    python tools/sst/fetch_sst.py --write-manifest   # one-off: record the hashes

The data is gitignored. 167 MB of JSON doesn't belong in the repo, and pinning
the commit plus hashing every file gives everyone byte-identical tests.
"""

import hashlib
import http.client
import io
import shutil
import sys
import tarfile
import time
import urllib.request
from pathlib import Path

COMMIT = "f9c30210245dd691661db39f5ace022c465ecc2f"
URL = f"https://codeload.github.com/SingleStepTests/sm83/tar.gz/{COMMIT}"
HERE = Path(__file__).resolve().parent
DATA = HERE / "data"
MANIFEST = HERE / "manifest.sha256"
EXPECTED_FILES = 500


def local_hashes() -> dict[str, str]:
    folder = DATA / "v1"
    if not folder.exists():
        return {}
    return {f"v1/{p.name}": hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.glob("*.json"))}


def read_manifest() -> dict[str, str]:
    entries = {}
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        if len(line) <= 66 or line[64:66] != "  ":
            raise SystemExit(f"error: malformed manifest line: {line!r}")
        entries[line[66:]] = line[:64]
    return entries


def download() -> bytes:
    last_error = None
    for attempt in range(1, 6):
        try:
            print(f"downloading {URL} (attempt {attempt}/5)")
            with urllib.request.urlopen(URL, timeout=300) as response:
                return response.read()
        except (OSError, http.client.HTTPException) as error:
            last_error = error
            time.sleep(5 * attempt)
    raise SystemExit(f"error: download failed after 5 attempts: {last_error}")


def extract(archive: bytes) -> int:
    target = DATA / "v1"
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    count = 0
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as tar:
        for member in tar.getmembers():
            parts = member.name.split("/")
            if member.isfile() and len(parts) == 3 and parts[1] == "v1" and parts[2].endswith(".json"):
                (target / parts[2]).write_bytes(tar.extractfile(member).read())
                count += 1
    return count


def main() -> int:
    if "--write-manifest" in sys.argv[1:]:
        if len(local_hashes()) != EXPECTED_FILES:
            extract(download())
        hashes = local_hashes()
        if len(hashes) != EXPECTED_FILES:
            raise SystemExit(f"error: expected {EXPECTED_FILES} files, found {len(hashes)}")
        text = "".join(f"{digest}  {name}\n" for name, digest in sorted(hashes.items()))
        MANIFEST.write_text(text, encoding="utf-8", newline="\n")
        print(f"wrote {MANIFEST} ({len(hashes)} files)")
        return 0

    expected = read_manifest()
    if local_hashes() == expected:
        print(f"test data present and verified ({len(expected)} files)")
        return 0
    count = extract(download())
    actual = local_hashes()
    if actual != expected:
        missing = sorted(set(expected) - set(actual))
        extra = sorted(set(actual) - set(expected))
        changed = sorted(n for n in set(expected) & set(actual) if expected[n] != actual[n])
        print(f"error: downloaded data does not match the manifest "
              f"(missing {len(missing)}, extra {len(extra)}, changed {len(changed)})")
        for name in (missing + extra + changed)[:20]:
            print(f"  {name}")
        return 1
    print(f"downloaded and verified {count} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Keep the manifest LF on every checkout. Append this line to `.gitattributes`:

```
tools/sst/manifest.sha256 text eol=lf
```

- [ ] **Step 6: Generate the manifest once, then verify**

```powershell
python tools/sst/fetch_sst.py --write-manifest
python tools/sst/fetch_sst.py
(Get-Content tools/sst/manifest.sha256 | Measure-Object -Line).Lines
Select-String -Path tools/sst/manifest.sha256 -Pattern "  v1/cb 00.json$" | Select-Object -ExpandProperty Line
```

Expected:
- `wrote ...manifest.sha256 (500 files)`
- then `test data present and verified (500 files)`
- then `500`
- then one line ending in `  v1/cb 00.json`

If the download fails five times, rerun it. The network here drops connections.

- [ ] **Step 7: Commit**

```powershell
git add .gitattributes CMakeLists.txt tools/sst/CMakeLists.txt tools/sst/Sha256.h tools/sst/Sha256.cpp tools/sst/Manifest.h tools/sst/Manifest.cpp tools/sst/Selection.h tools/sst/Selection.cpp tools/sst/fetch_sst.py tools/sst/manifest.sha256 tests/test_sha256.cpp tests/test_manifest.cpp tests/test_selection.cpp
git commit -m "feat: pin SingleStepTests by commit and SHA-256 manifest" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Loader, comparator and single-test runner

**Files:**
- Create: `tools/sst/SstLoader.h`, `tools/sst/SstLoader.cpp`, `tools/sst/SstCompare.h`, `tools/sst/SstCompare.cpp`, `tools/sst/SstRun.h`, `tools/sst/SstRun.cpp`
- Modify: `tools/sst/CMakeLists.txt`
- Test: `tests/test_sst_loader.cpp`, `tests/test_sst_run.cpp`

**Interfaces:**
- Consumes: `Cpu` (Task 3), `RecordingBus`/`Cycle` (Task 2).
- Produces: `struct sst::CpuSnapshot { u16 pc, sp; u8 a,b,c,d,e,f,h,l; bool ime; std::optional<bool> imePending; std::vector<std::pair<u16,u8>> ram; }`; `struct sst::SstTest { std::string name; CpuSnapshot initial, final; std::vector<Cycle> cycles; }`; `sst::parseTests(std::string_view) -> std::vector<SstTest>` (throws on any unknown, missing or out-of-range field).
- Produces: `struct sst::Mismatch { std::string field, expected, actual; int cycle = -1; }`; `sst::compareResult(const SstTest&, const fourshades::Cpu&, const RecordingBus&) -> std::optional<Mismatch>`.
- Produces: `enum class sst::Status { Pass, Fail, Unimplemented }`; `struct sst::TestOutcome { Status status; std::optional<Mismatch> mismatch; }`; `sst::runTest(const SstTest&, RecordingBus&) -> TestOutcome`.

- [ ] **Step 1: Write the failing tests**

`tests/test_sst_loader.cpp`:

```cpp
#include <doctest/doctest.h>

#include "sst/SstLoader.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace {
// One real-format test: NOP at 0x0100.
const std::string kNop =
    R"([{"name":"00 0000",)"
    R"("initial":{"pc":256,"sp":65534,"a":1,"b":2,"c":3,"d":4,"e":5,"f":176,"h":6,"l":7,"ime":1,"ie":0,"ram":[[256,0]]},)"
    R"("final":{"pc":257,"sp":65534,"a":1,"b":2,"c":3,"d":4,"e":5,"f":176,"h":6,"l":7,"ime":1,"ram":[[256,0]]},)"
    R"("cycles":[[256,0,"r-m"]]}])";

std::string with(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}
} // namespace

TEST_CASE("parseTests reads every field of a test") {
    const auto tests = sst::parseTests(kNop);
    REQUIRE(tests.size() == 1);
    const sst::SstTest& t = tests[0];
    CHECK(t.name == "00 0000");
    CHECK(t.initial.pc == 256);
    CHECK(t.initial.sp == 65534);
    CHECK(t.initial.f == 0xB0);
    CHECK(t.initial.ime);
    REQUIRE(t.initial.ram.size() == 1);
    CHECK(t.initial.ram[0] == std::pair<fourshades::u16, fourshades::u8>{256, 0});
    CHECK(t.final.pc == 257);
    CHECK_FALSE(t.final.imePending.has_value());
    REQUIRE(t.cycles.size() == 1);
    CHECK(t.cycles[0] == sst::Cycle{256, 0, sst::CycleKind::Read});
}

TEST_CASE("parseTests reads the EI 'ei' flag in the final state") {
    const auto tests = sst::parseTests(with(kNop, R"("ime":1,"ram":[[256,0]]},"cycles")",
                                            R"("ime":1,"ei":1,"ram":[[256,0]]},"cycles")"));
    REQUIRE(tests[0].final.imePending.has_value());
    CHECK(*tests[0].final.imePending);
}

TEST_CASE("parseTests rejects anything it doesn't understand") {
    CHECK_THROWS_AS(sst::parseTests(with(kNop, R"("ie":0)", R"("zz":0)")), std::runtime_error);
    CHECK_THROWS_AS(sst::parseTests(with(kNop, R"("a":1,"b")", R"("a":256,"b")")), std::runtime_error);
    CHECK_THROWS_AS(sst::parseTests(with(kNop, R"("r-m")", R"("r--")")), std::runtime_error);
    CHECK_THROWS_AS(sst::parseTests(with(kNop, R"("sp":65534,)", "")), std::runtime_error);
    CHECK_THROWS_AS(sst::parseTests(with(kNop, R"("name":"00 0000",)", R"("name":"00 0000","extra":1,)")),
                    std::runtime_error);
}
```

`tests/test_sst_run.cpp`:

```cpp
#include <doctest/doctest.h>

#include "sst/RecordingBus.h"
#include "sst/SstLoader.h"
#include "sst/SstRun.h"

#include <string>
#include <string_view>

namespace {
const std::string kNop =
    R"([{"name":"00 0000",)"
    R"("initial":{"pc":256,"sp":65534,"a":1,"b":2,"c":3,"d":4,"e":5,"f":176,"h":6,"l":7,"ime":1,"ie":0,"ram":[[256,0]]},)"
    R"("final":{"pc":257,"sp":65534,"a":1,"b":2,"c":3,"d":4,"e":5,"f":176,"h":6,"l":7,"ime":1,"ram":[[256,0]]},)"
    R"("cycles":[[256,0,"r-m"]]}])";

std::string with(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

sst::TestOutcome run(const std::string& json) {
    sst::RecordingBus bus;
    return sst::runTest(sst::parseTests(json).at(0), bus);
}
} // namespace

TEST_CASE("a correct result passes") {
    CHECK(run(kNop).status == sst::Status::Pass);
}

// These test the tester: each one breaks the expected result in one place
// and checks the comparator notices and names the right field.
TEST_CASE("a wrong register is caught") {
    const auto outcome = run(with(kNop, R"("pc":257,"sp":65534,"a":1)", R"("pc":257,"sp":65534,"a":9)"));
    REQUIRE(outcome.status == sst::Status::Fail);
    CHECK(outcome.mismatch->field == "a");
    CHECK(outcome.mismatch->expected == "0x09");
    CHECK(outcome.mismatch->actual == "0x01");
}

TEST_CASE("a wrong memory byte is caught") {
    const auto outcome = run(with(kNop, R"("ram":[[256,0]]},"cycles")", R"("ram":[[256,7]]},"cycles")"));
    REQUIRE(outcome.status == sst::Status::Fail);
    CHECK(outcome.mismatch->field == "ram[0x0100]");
}

TEST_CASE("a wrong cycle kind, count or bus value is caught") {
    const auto kind = run(with(kNop, R"([[256,0,"r-m"]])", R"([[256,0,"-wm"]])"));
    REQUIRE(kind.status == sst::Status::Fail);
    CHECK(kind.mismatch->field == "cycle kind");
    CHECK(kind.mismatch->cycle == 0);

    const auto count = run(with(kNop, R"([[256,0,"r-m"]])", R"([[256,0,"r-m"],[256,0,"---"]])"));
    REQUIRE(count.status == sst::Status::Fail);
    CHECK(count.mismatch->field == "cycle count");

    const auto bus = run(with(kNop, R"([[256,0,"r-m"]])", R"([[257,0,"r-m"]])"));
    REQUIRE(bus.status == sst::Status::Fail);
    CHECK(bus.mismatch->field == "cycle bus");
}

TEST_CASE("an expected pending EI is checked") {
    const auto outcome = run(with(kNop, R"("ime":1,"ram":[[256,0]]},"cycles")",
                                  R"("ime":1,"ei":1,"ram":[[256,0]]},"cycles")"));
    REQUIRE(outcome.status == sst::Status::Fail);
    CHECK(outcome.mismatch->field == "ei");
}

TEST_CASE("an unimplemented opcode is reported, not failed") {
    // Uses the CB stub from Task 3. Once Task 13 implements every CB opcode,
    // nothing can reach the unimplemented path, and Task 13 deletes this case.
    const std::string cb =
        R"([{"name":"cb 00 0000",)"
        R"("initial":{"pc":256,"sp":65534,"a":1,"b":2,"c":3,"d":4,"e":5,"f":176,"h":6,"l":7,"ime":1,"ie":0,"ram":[[256,203],[257,0]]},)"
        R"("final":{"pc":258,"sp":65534,"a":1,"b":4,"c":3,"d":4,"e":5,"f":0,"h":6,"l":7,"ime":1,"ram":[[256,203],[257,0]]},)"
        R"("cycles":[[256,203,"r-m"],[257,0,"r-m"]]}])";
    CHECK(run(cb).status == sst::Status::Unimplemented);
}
```

- [ ] **Step 2: Run to verify it fails**

```powershell
.\tools\dev.cmd cmake --build --preset release
```

Expected: FAIL with `cannot open include file: 'sst/SstLoader.h'`.

- [ ] **Step 3: Implement**

`tools/sst/SstLoader.h`:

```cpp
#pragma once

#include "sst/SstTypes.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sst {

struct CpuSnapshot {
    u16 pc = 0, sp = 0;
    u8 a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, h = 0, l = 0;
    bool ime = false;
    // Only the EI tests record this ("ei" in the final state). Absent means
    // no EI is pending.
    std::optional<bool> imePending;
    std::vector<std::pair<u16, u8>> ram;
};

struct SstTest {
    std::string name;
    CpuSnapshot initial;
    CpuSnapshot final;
    std::vector<Cycle> cycles;
};

// Parses one test file. Throws std::runtime_error (or a JSON parse error) on
// any unknown, missing or out-of-range field, so nothing is silently ignored.
std::vector<SstTest> parseTests(std::string_view jsonText);

} // namespace sst
```

`tools/sst/SstLoader.cpp`:

```cpp
#include "sst/SstLoader.h"

#include <nlohmann/json.hpp>

#include <set>
#include <stdexcept>

namespace sst {

namespace {

using nlohmann::json;

const std::set<std::string> kTestKeys{"name", "initial", "final", "cycles"};
const std::set<std::string> kInitialKeys{"pc", "sp", "a", "b", "c", "d", "e", "f", "h", "l", "ime", "ie", "ram"};
const std::set<std::string> kFinalKeys{"pc", "sp", "a", "b", "c", "d", "e", "f", "h", "l", "ime", "ei", "ram"};

[[noreturn]] void fail(const std::string& test, const std::string& what) {
    throw std::runtime_error("test '" + test + "': " + what);
}

void checkKeys(const json& object, const std::set<std::string>& allowed, const std::string& test,
               const std::string& where) {
    if (!object.is_object()) {
        fail(test, where + " is not an object");
    }
    for (const auto& item : object.items()) {
        if (!allowed.contains(item.key())) {
            fail(test, "unknown key '" + item.key() + "' in " + where);
        }
    }
}

unsigned number(const json& value, unsigned max, const std::string& test, const std::string& what) {
    if (!value.is_number_unsigned() || value.get<unsigned>() > max) {
        fail(test, "bad value for " + what);
    }
    return value.get<unsigned>();
}

unsigned field(const json& object, const char* key, unsigned max, const std::string& test) {
    if (!object.contains(key)) {
        fail(test, std::string("missing key '") + key + "'");
    }
    return number(object.at(key), max, test, key);
}

CpuSnapshot snapshot(const json& object, bool isFinal, const std::string& test) {
    checkKeys(object, isFinal ? kFinalKeys : kInitialKeys, test, isFinal ? "final" : "initial");
    CpuSnapshot s;
    s.pc = static_cast<u16>(field(object, "pc", 0xFFFF, test));
    s.sp = static_cast<u16>(field(object, "sp", 0xFFFF, test));
    s.a = static_cast<u8>(field(object, "a", 0xFF, test));
    s.b = static_cast<u8>(field(object, "b", 0xFF, test));
    s.c = static_cast<u8>(field(object, "c", 0xFF, test));
    s.d = static_cast<u8>(field(object, "d", 0xFF, test));
    s.e = static_cast<u8>(field(object, "e", 0xFF, test));
    s.f = static_cast<u8>(field(object, "f", 0xFF, test));
    s.h = static_cast<u8>(field(object, "h", 0xFF, test));
    s.l = static_cast<u8>(field(object, "l", 0xFF, test));
    s.ime = field(object, "ime", 1, test) == 1;
    if (isFinal) {
        if (object.contains("ei")) {
            s.imePending = field(object, "ei", 1, test) == 1;
        }
    } else {
        // "ie" is in every initial state and no final one: the generator's own
        // interrupt-enable latch (0 or 1, not the 0xFFFF register). Interrupt
        // delivery is piece 2, so it is validated here and otherwise unused.
        field(object, "ie", 1, test);
    }
    if (!object.contains("ram") || !object.at("ram").is_array()) {
        fail(test, "missing ram");
    }
    for (const json& entry : object.at("ram")) {
        if (!entry.is_array() || entry.size() != 2) {
            fail(test, "bad ram entry");
        }
        s.ram.emplace_back(static_cast<u16>(number(entry[0], 0xFFFF, test, "ram address")),
                           static_cast<u8>(number(entry[1], 0xFF, test, "ram value")));
    }
    return s;
}

Cycle cycle(const json& entry, const std::string& test) {
    if (!entry.is_array() || entry.size() != 3 || !entry[2].is_string()) {
        fail(test, "bad cycle entry");
    }
    const std::string kind = entry[2].get<std::string>();
    Cycle c;
    c.address = static_cast<u16>(number(entry[0], 0xFFFF, test, "cycle address"));
    c.value = static_cast<u8>(number(entry[1], 0xFF, test, "cycle value"));
    if (kind == "r-m") {
        c.kind = CycleKind::Read;
    } else if (kind == "-wm") {
        c.kind = CycleKind::Write;
    } else if (kind == "---") {
        c.kind = CycleKind::Idle;
    } else {
        fail(test, "unknown cycle kind '" + kind + "'");
    }
    return c;
}

} // namespace

std::vector<SstTest> parseTests(std::string_view jsonText) {
    const json document = json::parse(jsonText);
    if (!document.is_array()) {
        throw std::runtime_error("test file is not a JSON array");
    }
    std::vector<SstTest> tests;
    tests.reserve(document.size());
    for (const json& item : document) {
        const std::string name = item.is_object() && item.contains("name") && item.at("name").is_string()
                                     ? item.at("name").get<std::string>()
                                     : std::string("<unnamed>");
        checkKeys(item, kTestKeys, name, "test");
        if (!item.contains("initial") || !item.contains("final") || !item.contains("cycles") ||
            !item.at("cycles").is_array()) {
            fail(name, "missing initial, final or cycles");
        }
        SstTest test;
        test.name = name;
        test.initial = snapshot(item.at("initial"), false, name);
        test.final = snapshot(item.at("final"), true, name);
        for (const json& entry : item.at("cycles")) {
            test.cycles.push_back(cycle(entry, name));
        }
        tests.push_back(std::move(test));
    }
    return tests;
}

} // namespace sst
```

`tools/sst/SstCompare.h`:

```cpp
#pragma once

#include "core/Cpu.h"
#include "sst/RecordingBus.h"
#include "sst/SstLoader.h"

#include <optional>
#include <string>

namespace sst {

struct Mismatch {
    std::string field;    // "a", "pc", "ei", "ram[0x1234]", "cycle count", "cycle kind", "cycle bus"
    std::string expected;
    std::string actual;
    int cycle = -1;       // index into the cycle list for cycle mismatches
};

// The first difference between what the test expects and what the CPU and
// bus ended up with, or nullopt if they match. Idle cycles are compared by
// kind only; their address and value are the generator's leftover bus
// contents, not something software can observe (design spec, "Match the
// test model").
std::optional<Mismatch> compareResult(const SstTest& test, const fourshades::Cpu& cpu, const RecordingBus& bus);

} // namespace sst
```

`tools/sst/SstCompare.cpp`:

```cpp
#include "sst/SstCompare.h"

#include <cstdio>

namespace sst {

namespace {

std::string hex(unsigned value, int digits) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%0*X", digits, value);
    return buffer;
}

const char* kindName(CycleKind kind) {
    switch (kind) {
    case CycleKind::Read: return "r-m";
    case CycleKind::Write: return "-wm";
    default: return "---";
    }
}

} // namespace

std::optional<Mismatch> compareResult(const SstTest& test, const fourshades::Cpu& cpu, const RecordingBus& bus) {
    const CpuSnapshot& want = test.final;
    const fourshades::Registers& r = cpu.regs;

    const struct {
        const char* name;
        unsigned expected;
        unsigned actual;
        int digits;
    } registers[] = {
        {"a", want.a, r.a, 2},   {"b", want.b, r.b, 2},   {"c", want.c, r.c, 2},
        {"d", want.d, r.d, 2},   {"e", want.e, r.e, 2},   {"f", want.f, r.f(), 2},
        {"h", want.h, r.h, 2},   {"l", want.l, r.l, 2},   {"sp", want.sp, r.sp, 4},
        {"pc", want.pc, r.pc, 4},
    };
    for (const auto& reg : registers) {
        if (reg.expected != reg.actual) {
            return Mismatch{reg.name, hex(reg.expected, reg.digits), hex(reg.actual, reg.digits)};
        }
    }
    if (want.ime != cpu.ime) {
        return Mismatch{"ime", want.ime ? "1" : "0", cpu.ime ? "1" : "0"};
    }
    const bool pending = want.imePending.value_or(false);
    if (pending != cpu.imePending()) {
        return Mismatch{"ei", pending ? "1" : "0", cpu.imePending() ? "1" : "0"};
    }

    for (const auto& [address, value] : want.ram) {
        if (bus.peek(address) != value) {
            return Mismatch{"ram[" + hex(address, 4) + "]", hex(value, 2), hex(bus.peek(address), 2)};
        }
    }

    const auto& got = bus.log();
    if (got.size() != test.cycles.size()) {
        return Mismatch{"cycle count", std::to_string(test.cycles.size()), std::to_string(got.size())};
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        const Cycle& expected = test.cycles[i];
        const Cycle& actual = got[i];
        if (expected.kind != actual.kind) {
            return Mismatch{"cycle kind", kindName(expected.kind), kindName(actual.kind), static_cast<int>(i)};
        }
        if (expected.kind != CycleKind::Idle &&
            (expected.address != actual.address || expected.value != actual.value)) {
            return Mismatch{"cycle bus", hex(expected.address, 4) + "=" + hex(expected.value, 2),
                            hex(actual.address, 4) + "=" + hex(actual.value, 2), static_cast<int>(i)};
        }
    }
    return std::nullopt;
}

} // namespace sst
```

`tools/sst/SstRun.h`:

```cpp
#pragma once

#include "sst/RecordingBus.h"
#include "sst/SstCompare.h"
#include "sst/SstLoader.h"

#include <optional>

namespace sst {

enum class Status { Pass, Fail, Unimplemented };

struct TestOutcome {
    Status status = Status::Fail;
    std::optional<Mismatch> mismatch;
};

// Runs one test on a fresh CPU, using `bus` (reset first) as its memory.
TestOutcome runTest(const SstTest& test, RecordingBus& bus);

} // namespace sst
```

`tools/sst/SstRun.cpp`:

```cpp
#include "sst/SstRun.h"

#include "core/Cpu.h"

namespace sst {

TestOutcome runTest(const SstTest& test, RecordingBus& bus) {
    bus.reset();
    for (const auto& [address, value] : test.initial.ram) {
        bus.poke(address, value);
    }

    fourshades::Cpu cpu(bus);
    fourshades::Registers& r = cpu.regs;
    r.a = test.initial.a;
    r.b = test.initial.b;
    r.c = test.initial.c;
    r.d = test.initial.d;
    r.e = test.initial.e;
    r.setF(test.initial.f);
    r.h = test.initial.h;
    r.l = test.initial.l;
    r.sp = test.initial.sp;
    r.pc = test.initial.pc;
    cpu.ime = test.initial.ime;

    cpu.step();
    // A halted or stopped CPU spends every further M-cycle idle. The HALT and
    // STOP tests record a fixed window of cycles, so keep stepping until the
    // window is full. For every other instruction the CPU is still Running
    // and this loop doesn't run.
    while (cpu.state() != fourshades::Cpu::State::Running && bus.log().size() < test.cycles.size()) {
        cpu.step();
    }

    if (cpu.unimplemented()) {
        return {Status::Unimplemented, std::nullopt};
    }
    if (auto mismatch = compareResult(test, cpu, bus)) {
        return {Status::Fail, std::move(mismatch)};
    }
    return {Status::Pass, std::nullopt};
}

} // namespace sst
```

In `tools/sst/CMakeLists.txt`, replace the `add_library` block with:

```cmake
add_library(sst_harness STATIC
    Sha256.cpp
    Manifest.cpp
    Selection.cpp
    SstLoader.cpp
    SstCompare.cpp
    SstRun.cpp
)
```

- [ ] **Step 4: Run to verify it passes**

```powershell
.\tools\dev.cmd cmake --build --preset release
.\tools\dev.cmd ctest --preset release
```

Expected: `100% tests passed`.

- [ ] **Step 5: Commit**

```powershell
git add tools/sst/CMakeLists.txt tools/sst/SstLoader.h tools/sst/SstLoader.cpp tools/sst/SstCompare.h tools/sst/SstCompare.cpp tools/sst/SstRun.h tools/sst/SstRun.cpp tests/test_sst_loader.cpp tests/test_sst_run.cpp
git commit -m "feat: strict test loader, comparator and single-test runner, with tests of the tester" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: The runner executable and the first real score

**Files:**
- Create: `tools/sst/sst_runner.cpp`
- Modify: `tools/sst/CMakeLists.txt`

**Interfaces:**
- Consumes: everything in `sst_harness` (Tasks 4-5).
- Produces: `build/release/tools/sst/sst_runner.exe [--data DIR] [--manifest FILE] [--out FILE] [--only LIST]`. Defaults are `tools/sst/data`, `tools/sst/manifest.sha256`, `build/sst-results.json`, run from the repo root. Exit code 0 means a score was produced (whatever the score); 2 means a harness error.
- Produces: the results file format read by Task 7:

```json
{"suite": "SingleStepTests/sm83", "commit": "f9c3…", "partial": false,
 "total_files": 500, "passing_files": 5, "elapsed_seconds": 12.3,
 "files": [{"name": "00", "status": "pass", "passed": 1000, "tests": 1000, "first_failure": null}, …]}
```

`status` is `"pass"`, `"fail"` or `"unimplemented"`. `first_failure` is `null` or `{"test", "field", "expected", "actual", "cycle"}`.

- [ ] **Step 1: Write the runner**

`tools/sst/sst_runner.cpp`:

```cpp
// Runs the SingleStepTests SM83 suite against the FourShades CPU and writes
// a results file for tools/scoreboard.py. Run from the repository root.
//
//   sst_runner                      full run: all 500 files, writes build/sst-results.json
//   sst_runner --only 40-7f,cb00-cb0f  a subset, marked partial (the scoreboard refuses it)

#include "sst/Manifest.h"
#include "sst/RecordingBus.h"
#include "sst/Selection.h"
#include "sst/SstLoader.h"
#include "sst/SstRun.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kCommit = "f9c30210245dd691661db39f5ace022c465ecc2f";
constexpr std::size_t kExpectedFiles = 500;

struct Options {
    std::filesystem::path data = "tools/sst/data";
    std::filesystem::path manifest = "tools/sst/manifest.sha256";
    std::filesystem::path out = "build/sst-results.json";
    std::string only;
};

int usage() {
    std::cerr << "usage: sst_runner [--data DIR] [--manifest FILE] [--out FILE] [--only LIST]\n"
                 "  LIST: comma-separated opcodes or ranges, e.g. 40-7f,06,cb00-cbff\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) {
            return usage();
        }
        if (arg == "--data") {
            options.data = argv[++i];
        } else if (arg == "--manifest") {
            options.manifest = argv[++i];
        } else if (arg == "--out") {
            options.out = argv[++i];
        } else if (arg == "--only") {
            options.only = argv[++i];
        } else {
            return usage();
        }
    }

    try {
        const auto entries = sst::parseManifest(sst::readBinaryFile(options.manifest));
        if (entries.size() != kExpectedFiles) {
            throw std::runtime_error("manifest lists " + std::to_string(entries.size()) + " files, expected 500");
        }
        std::vector<std::string> all;
        for (const auto& entry : entries) {
            all.push_back(sst::stemOf(entry.path));
        }
        const bool partial = !options.only.empty();
        const std::vector<std::string> selected = partial ? sst::expandSelection(options.only, all) : all;
        if (selected.empty()) {
            throw std::runtime_error("--only matched no test files");
        }

        auto problems = sst::verifyFiles(entries, options.data, selected);
        if (!partial) {
            const auto extra = sst::unexpectedFiles(entries, options.data);
            problems.insert(problems.end(), extra.begin(), extra.end());
        }
        if (!problems.empty()) {
            for (const auto& problem : problems) {
                std::cerr << "data check: " << problem << '\n';
            }
            std::cerr << "refusing to run: test data does not match tools/sst/manifest.sha256\n"
                         "run: python tools/sst/fetch_sst.py\n";
            return 2;
        }

        const auto start = std::chrono::steady_clock::now();
        sst::RecordingBus bus;
        nlohmann::json files = nlohmann::json::array();
        std::size_t passingFiles = 0;
        std::size_t unimplementedFiles = 0;

        for (const std::string& stem : selected) {
            const auto tests = sst::parseTests(sst::readBinaryFile(options.data / "v1" / (stem + ".json")));
            std::size_t passed = 0;
            bool unimplemented = false;
            nlohmann::json firstFailure = nullptr;
            for (const sst::SstTest& test : tests) {
                const sst::TestOutcome outcome = sst::runTest(test, bus);
                if (outcome.status == sst::Status::Pass) {
                    ++passed;
                } else if (outcome.status == sst::Status::Unimplemented) {
                    unimplemented = true;
                    break;
                } else if (firstFailure.is_null()) {
                    const sst::Mismatch& m = *outcome.mismatch;
                    firstFailure = {{"test", test.name}, {"field", m.field}, {"expected", m.expected},
                                    {"actual", m.actual}, {"cycle", m.cycle}};
                }
            }

            std::string status = "fail";
            if (unimplemented) {
                status = "unimplemented";
                ++unimplementedFiles;
            } else if (passed == tests.size()) {
                status = "pass";
                ++passingFiles;
            } else {
                std::cout << "FAIL " << stem << "  " << passed << "/" << tests.size() << "  first: "
                          << firstFailure["test"].get<std::string>() << "  "
                          << firstFailure["field"].get<std::string>() << " expected "
                          << firstFailure["expected"].get<std::string>() << " got "
                          << firstFailure["actual"].get<std::string>();
                if (firstFailure["cycle"].get<int>() >= 0) {
                    std::cout << " (cycle " << firstFailure["cycle"].get<int>() << ")";
                }
                std::cout << '\n';
            }
            files.push_back({{"name", stem}, {"status", status}, {"passed", passed},
                             {"tests", tests.size()}, {"first_failure", firstFailure}});
        }

        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const nlohmann::json results = {
            {"suite", "SingleStepTests/sm83"}, {"commit", kCommit},        {"partial", partial},
            {"total_files", selected.size()},  {"passing_files", passingFiles},
            {"elapsed_seconds", seconds},      {"files", files},
        };
        if (options.out.has_parent_path()) {
            std::filesystem::create_directories(options.out.parent_path());
        }
        std::ofstream(options.out) << results.dump(1) << '\n';

        std::printf("%s%zu / %zu passing  (%zu unimplemented, %.1f s)\nresults: %s\n",
                    partial ? "selected: " : "CPU instructions: ", passingFiles, selected.size(),
                    unimplementedFiles, seconds, options.out.string().c_str());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
```

Append to `tools/sst/CMakeLists.txt`:

```cmake
add_executable(sst_runner sst_runner.cpp)
target_link_libraries(sst_runner PRIVATE sst_harness)
```

- [ ] **Step 2: Build and run the full suite**

```powershell
.\tools\dev.cmd cmake --build --preset release
python tools/sst/fetch_sst.py
.\build\release\tools\sst\sst_runner.exe
```

Expected final line before `results:`: `CPU instructions: 5 / 500 passing  (495 unimplemented, …)`. The 5 are `00 10 76 f3 fb`, the instructions Task 3 implemented. There should be no `FAIL` lines. If any of those 5 fails, or the count isn't 5, stop and fix it before continuing. Either the CPU skeleton or the harness is wrong, and every later task depends on both.

- [ ] **Step 3: Check that the data guard works**

Change one byte of a test file, confirm the runner refuses, then restore:

```powershell
Add-Content -Path "tools/sst/data/v1/00.json" -Value " "
.\build\release\tools\sst\sst_runner.exe; "exit code: $LASTEXITCODE"
python tools/sst/fetch_sst.py
```

Expected: `data check: v1/00.json: hash does not match the manifest`, then `refusing to run…`, then `exit code: 2`. The fetch then re-downloads and prints `downloaded and verified 500 files`.

- [ ] **Step 4: Commit**

```powershell
git add tools/sst/CMakeLists.txt tools/sst/sst_runner.cpp
git commit -m "feat: SingleStepTests runner; first score 5/500" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: Scoreboard, isolation check, agent rules, README

**Files:**
- Create: `tools/scoreboard.py`, `tools/test_scoreboard.py`, `tools/check_core_isolation.py`, `CLAUDE.md`, `docs/known-divergences.md`
- Create (generated): `scoreboard.json`
- Modify: `README.md`

**Interfaces:**
- Consumes: the results file from Task 6.
- Produces: `python tools/scoreboard.py update|check <results.json>`. `check` exits 1 on mismatch. Also `scoreboard.run(mode, results_path, readme_path, board_path) -> int` for the tests.
- Produces: `python tools/check_core_isolation.py`, exiting 1 with file:line messages on a violation.

- [ ] **Step 1: Write the failing scoreboard tests**

`tools/test_scoreboard.py`:

```python
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scoreboard  # noqa: E402


def results(passing, partial=False, total=500):
    files = [{"name": f"{i:03}", "status": "pass" if i < passing else "fail"} for i in range(total)]
    return {"suite": "SingleStepTests/sm83", "commit": "abc", "partial": partial,
            "total_files": total, "passing_files": passing, "files": files}


class ScoreboardTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.readme = self.dir / "README.md"
        self.board = self.dir / "scoreboard.json"
        self.results = self.dir / "results.json"
        self.readme.write_text("intro\n<!-- scoreboard:start -->\nold\n<!-- scoreboard:end -->\noutro\n",
                               encoding="utf-8")

    def write_results(self, data):
        self.results.write_text(json.dumps(data), encoding="utf-8")

    def run_mode(self, mode):
        return scoreboard.run(mode, self.results, self.readme, self.board)

    def test_line_format(self):
        self.assertEqual(scoreboard.line("cpu instructions", 250, 500),
                         "cpu instructions  " + "█" * 8 + "░" * 8 + "   250 / 500")
        self.assertEqual(scoreboard.line("test roms", 0, 1300),
                         "test roms         " + "░" * 16 + "     0 / 1300")

    def test_update_then_check_passes(self):
        self.write_results(results(250))
        self.assertEqual(self.run_mode("update"), 0)
        self.assertEqual(self.run_mode("check"), 0)
        text = self.readme.read_text(encoding="utf-8")
        self.assertIn("250 / 500", text)
        self.assertTrue(text.startswith("intro\n"))
        self.assertTrue(text.endswith("outro\n"))
        self.assertEqual(json.loads(self.board.read_text(encoding="utf-8"))["cpu_instructions"]["passing"], 250)

    def test_check_fails_when_readme_is_edited_by_hand(self):
        self.write_results(results(250))
        self.run_mode("update")
        text = self.readme.read_text(encoding="utf-8").replace("250 / 500", "499 / 500")
        self.readme.write_text(text, encoding="utf-8")
        self.assertEqual(self.run_mode("check"), 1)

    def test_check_fails_when_scoreboard_json_is_missing(self):
        self.write_results(results(250))
        self.run_mode("update")
        self.board.unlink()
        self.assertEqual(self.run_mode("check"), 1)

    def test_partial_results_are_refused(self):
        self.write_results(results(5, partial=True))
        with self.assertRaises(SystemExit):
            self.run_mode("update")

    def test_inconsistent_results_are_refused(self):
        data = results(10)
        data["passing_files"] = 11
        self.write_results(data)
        with self.assertRaises(SystemExit):
            self.run_mode("update")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify it fails**

```powershell
python -m unittest discover -s tools -p "test_*.py"
```

Expected: FAIL with `ModuleNotFoundError: No module named 'scoreboard'`.

- [ ] **Step 3: Implement the scripts**

`tools/scoreboard.py`:

```python
"""
Generate the README scoreboard and scoreboard.json from a SingleStepTests
results file.

    python tools/scoreboard.py update build/sst-results.json
    python tools/scoreboard.py check  build/sst-results.json

`check` exits 1 if the committed README block or scoreboard.json differs from
what the results say. CI runs it, so a hand-edited score fails the build,
whether it's too high or too low.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
START = "<!-- scoreboard:start -->"
END = "<!-- scoreboard:end -->"
BAR = 16
CPU_FILES = 500
# Piece 2 replaces this with the test-ROM runner's results.
TEST_ROMS = (0, 1300)


def load_results(path):
    results = json.loads(Path(path).read_text(encoding="utf-8"))
    if results.get("partial"):
        raise SystemExit("error: these are partial results (--only was used); run the full suite")
    files = results.get("files", [])
    if results.get("total_files") != CPU_FILES or len(files) != CPU_FILES:
        raise SystemExit(f"error: expected results for {CPU_FILES} files")
    passing = sum(1 for f in files if f["status"] == "pass")
    if passing != results.get("passing_files"):
        raise SystemExit("error: results file is inconsistent (passing_files does not match the file list)")
    return results


def scoreboard(results):
    passing = results["passing_files"]
    return {
        "cpu_instructions": {"passing": passing, "total": CPU_FILES},
        "test_roms": {"passing": TEST_ROMS[0], "total": TEST_ROMS[1]},
        "source": {"suite": results["suite"], "commit": results["commit"]},
    }


def line(label, passing, total):
    filled = BAR * passing // total
    return f"{label:<18}{'█' * filled}{'░' * (BAR - filled)}  {passing:>4} / {total}"


def readme_block(board):
    cpu, roms = board["cpu_instructions"], board["test_roms"]
    return "\n".join([
        START,
        "```",
        line("cpu instructions", cpu["passing"], cpu["total"]),
        line("test roms", roms["passing"], roms["total"]),
        "```",
        END,
    ])


def replace_block(text, block):
    start, end = text.find(START), text.find(END)
    if start == -1 or end == -1 or end < start:
        raise SystemExit("error: README has no scoreboard markers")
    return text[:start] + block + text[end + len(END):]


def run(mode, results_path, readme_path, board_path):
    board = scoreboard(load_results(results_path))
    board_json = json.dumps(board, indent=2) + "\n"
    readme = Path(readme_path).read_text(encoding="utf-8")
    new_readme = replace_block(readme, readme_block(board))

    if mode == "update":
        Path(readme_path).write_text(new_readme, encoding="utf-8")
        Path(board_path).write_text(board_json, encoding="utf-8")
        print(f"scoreboard updated: cpu instructions {board['cpu_instructions']['passing']} / {CPU_FILES}")
        return 0

    problems = []
    if new_readme != readme:
        problems.append("README.md scoreboard block")
    board_file = Path(board_path)
    if not board_file.exists() or board_file.read_text(encoding="utf-8") != board_json:
        problems.append("scoreboard.json")
    if problems:
        print("scoreboard does not match the test results: " + ", ".join(problems))
        print("run: python tools/scoreboard.py update build/sst-results.json")
        return 1
    print("scoreboard matches the test results")
    return 0


def main(argv):
    if len(argv) != 3 or argv[1] not in ("update", "check"):
        raise SystemExit(__doc__)
    return run(argv[1], argv[2], ROOT / "README.md", ROOT / "scoreboard.json")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

`tools/check_core_isolation.py`:

```python
"""
Fail if the emulator core depends on the test harness or on test data.

The core may include only its own headers (core/...) and standard headers, and
may not mention files, JSON or the test suite at all. That keeps it
impossible, not just discouraged, for core code to look at the tests.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORE = ROOT / "src" / "core"
FORBIDDEN = re.compile(r"\b(sst|json|fopen|ifstream|ofstream|fstream|singlesteptests)\b", re.IGNORECASE)
INCLUDE = re.compile(r'#\s*include\s*[<"]([^">]+)[">]')


def main():
    problems = []
    for path in sorted(CORE.rglob("*")):
        if path.suffix not in (".h", ".hpp", ".cpp"):
            continue
        for number, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            where = f"{path.relative_to(ROOT).as_posix()}:{number}"
            if FORBIDDEN.search(text):
                problems.append(f"{where}: forbidden word: {text.strip()}")
            include = INCLUDE.search(text)
            if include and "/" in include.group(1) and not include.group(1).startswith("core/"):
                problems.append(f"{where}: include from outside the core: {include.group(1)}")
    if problems:
        print("core isolation check failed:")
        for problem in problems:
            print("  " + problem)
        return 1
    print("core isolation check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the Python checks**

```powershell
python -m unittest discover -s tools -p "test_*.py"
python tools/check_core_isolation.py
```

Expected: `OK` (6 tests), then `core isolation check passed`.

Prove the isolation check can fail: temporarily add the line `// json` to the end of `src/core/Types.h`, run the check, and expect `core isolation check failed:` naming `src/core/Types.h`. Then remove the line and confirm it passes again.

- [ ] **Step 5: Agent rules and the divergence log**

`CLAUDE.md`:

```markdown
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
```

`docs/known-divergences.md`:

```markdown
# Known divergences

Places where a test expects something that Pan Docs says the hardware doesn't
do. We follow Pan Docs and leave the test failing, so the scoreboard never
claims more than the hardware documentation supports.

Each entry gives the test, what it expects, what Pan Docs says (with a link),
and what FourShades does.

_None recorded yet._
```

- [ ] **Step 6: Put the scoreboard markers into the README and generate it**

In `README.md`, replace this block:

````
```
test roms  ░░░░░░░░░░░░░░░░  0 / 1300
```

**Status: not started.** There is no emulator code in this repository yet. The
scoreboard above is real, and it will stay at zero until it isn't.
````

with:

````
<!-- scoreboard:start -->
<!-- scoreboard:end -->

**Status: building the CPU (piece 1 of 6).** The CPU line counts SM83
instructions passing every one of their 1,000
[SingleStepTests](https://github.com/SingleStepTests/sm83), which check every
register, every byte of memory and every bus cycle. The test-ROM line starts
moving in piece 2. Both lines are generated from a real test run, and CI fails
any commit whose scoreboard doesn't match what the code actually scores.
````

In the "Repository layout" section, replace the code block and the sentence
`Source, tests and build files arrive with the first task.` with:

````
```
src/core/                 the emulator core: CPU and bus (no window, no files)
tests/                    unit tests (doctest)
tools/sst/                the SingleStepTests harness and pinned-data manifest
tools/scoreboard.py       turns a test run into the scoreboard above
docs/superpowers/specs/   the reasoning behind each piece
docs/superpowers/plans/   task-by-task implementation plans
docs/known-divergences.md where a test and the hardware documentation disagree
```

## Building

Windows and Visual Studio 2026 (with the C++ workload). Open the folder in
Visual Studio, or from PowerShell:

```powershell
.\tools\dev.cmd cmake --preset release
.\tools\dev.cmd cmake --build --preset release
.\tools\dev.cmd ctest --preset release
python tools/sst/fetch_sst.py              # the test data, pinned and hash-checked
.\build\release\tools\sst\sst_runner.exe   # score the CPU
```
````

In "Planned scope", replace the sentence beginning `The first milestone is not "it plays Tetris".` (through `reads something other than zero.`) with:

```
The first milestone is not "it plays Tetris". It is **the CPU passing every
SingleStepTests instruction, then Blargg's first test ROM**, at which point the
test-ROM line reads something other than zero.
```

Then generate the scoreboard from the Task 6 results:

```powershell
.\build\release\tools\sst\sst_runner.exe
python tools/scoreboard.py update build/sst-results.json
python tools/scoreboard.py check build/sst-results.json
Get-Content scoreboard.json
```

Expected: `scoreboard updated: cpu instructions 5 / 500`, then `scoreboard matches the test results`, then JSON with `"passing": 5`. The README now shows both scoreboard lines.

- [ ] **Step 7: Commit**

```powershell
git add tools/scoreboard.py tools/test_scoreboard.py tools/check_core_isolation.py CLAUDE.md docs/known-divergences.md README.md scoreboard.json
git commit -m "feat: generated scoreboard with CI check, core isolation check, agent rules" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 8: Windows CI

**Files:**
- Create: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: presets (Task 1), `fetch_sst.py` (Task 4), runner (Task 6), `scoreboard.py` and `check_core_isolation.py` (Task 7).

- [ ] **Step 1: Write the workflow**

`.github/workflows/ci.yml`:

```yaml
name: ci

on:
  push:
  pull_request:

jobs:
  build-and-score:
    runs-on: windows-latest
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4

      # Puts MSVC, and Visual Studio's bundled CMake and Ninja, on PATH.
      - uses: ilammy/msvc-dev-cmd@v1

      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"

      - name: Configure
        run: cmake --preset release

      - name: Build
        run: cmake --build --preset release

      - name: Unit tests
        run: ctest --preset release

      - name: Scoreboard script tests
        run: python -m unittest discover -s tools -p "test_*.py"

      - name: Core isolation
        run: python tools/check_core_isolation.py

      - uses: actions/cache@v4
        with:
          path: tools/sst/data
          key: sst-f9c30210245dd691661db39f5ace022c465ecc2f

      - name: Fetch SingleStepTests (pinned, hash-checked)
        run: python tools/sst/fetch_sst.py

      - name: Run SingleStepTests
        run: .\build\release\tools\sst\sst_runner.exe

      - name: Scoreboard matches the run
        run: python tools/scoreboard.py check build/sst-results.json
```

- [ ] **Step 2: Commit and push**

```powershell
git add .github/workflows/ci.yml
git commit -m "ci: build, test and recompute the scoreboard on Windows" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
git push origin main
```

- [ ] **Step 3: Verify CI went green**

Wait for the run, then check it through the public API (the repo is public, so no token is needed):

```powershell
$sha = git rev-parse HEAD
(Invoke-RestMethod "https://api.github.com/repos/doozleb/FourShades/commits/$sha/check-runs").check_runs | Select-Object name, status, conclusion
```

Expected: `build-and-score  completed  success`. If it's still `in_progress`, wait a minute and query again. If it fails, read the log at `https://github.com/doozleb/FourShades/actions`, fix the cause in a new commit, and push again. Don't move on with CI red.

If the log says CMake can't find Ninja, add this step directly after
`ilammy/msvc-dev-cmd@v1` and push again:

```yaml
      - uses: seanmiddleditch/gha-setup-ninja@v5
```

---

### Task 9: 8-bit loads (85 instructions)

**Files:**
- Modify (replace whole file): `src/core/CpuLoads8.cpp`
- Modify (generated): `README.md` scoreboard block, `scoreboard.json`

**Interfaces:**
- Consumes: `fetch8`, `fetch16`, `readR8`, `writeR8`, `bus_`, `regs` (Task 3).
- Produces: `Cpu::executeLoads8(u8) -> bool`. Returns true for exactly: 40-7f except 76; 06 0e 16 1e 26 2e 36 3e; 02 12 22 32 0a 1a 2a 3a; e0 f0 e2 f2 ea fa.

Selection used below:
Selection: `40-7f,06,0e,16,1e,26,2e,36,3e,02,12,22,32,0a,1a,2a,3a,e0,f0,e2,f2,ea,fa`
It covers 86 files: these 85, plus 76 (HALT, already passing).

- [ ] **Step 1: Run the tests to see them fail**

```powershell
.\build\release\tools\sst\sst_runner.exe --only "40-7f,06,0e,16,1e,26,2e,36,3e,02,12,22,32,0a,1a,2a,3a,e0,f0,e2,f2,ea,fa" --out build/sst-partial.json
```

Expected: `selected: 1 / 86 passing  (85 unimplemented, …)`.

- [ ] **Step 2: Implement**

`src/core/CpuLoads8.cpp`:

```cpp
#include "core/Cpu.h"

namespace fourshades {

// 8-bit loads. (HL) as an operand goes through readR8/writeR8 index 6, which
// costs the memory cycle at the right point.
bool Cpu::executeLoads8(u8 opcode) {
    const int y = (opcode >> 3) & 7;
    const int z = opcode & 7;

    if (opcode >= 0x40 && opcode <= 0x7F && opcode != 0x76) { // LD r, r'
        writeR8(y, readR8(z));
        return true;
    }
    if (opcode < 0x40 && z == 6) { // LD r, n (06 0E 16 1E 26 2E 36 3E)
        writeR8(y, fetch8());
        return true;
    }

    switch (opcode) {
    case 0x02: bus_.write(regs.bc(), regs.a); return true;
    case 0x12: bus_.write(regs.de(), regs.a); return true;
    case 0x22: // LD (HL+), A
        bus_.write(regs.hl(), regs.a);
        regs.setHl(static_cast<u16>(regs.hl() + 1));
        return true;
    case 0x32: // LD (HL-), A
        bus_.write(regs.hl(), regs.a);
        regs.setHl(static_cast<u16>(regs.hl() - 1));
        return true;
    case 0x0A: regs.a = bus_.read(regs.bc()); return true;
    case 0x1A: regs.a = bus_.read(regs.de()); return true;
    case 0x2A: // LD A, (HL+)
        regs.a = bus_.read(regs.hl());
        regs.setHl(static_cast<u16>(regs.hl() + 1));
        return true;
    case 0x3A: // LD A, (HL-)
        regs.a = bus_.read(regs.hl());
        regs.setHl(static_cast<u16>(regs.hl() - 1));
        return true;
    case 0xE0: { // LDH (n), A
        const u8 n = fetch8();
        bus_.write(static_cast<u16>(0xFF00 + n), regs.a);
        return true;
    }
    case 0xF0: { // LDH A, (n)
        const u8 n = fetch8();
        regs.a = bus_.read(static_cast<u16>(0xFF00 + n));
        return true;
    }
    case 0xE2: bus_.write(static_cast<u16>(0xFF00 + regs.c), regs.a); return true; // LD (C), A
    case 0xF2: regs.a = bus_.read(static_cast<u16>(0xFF00 + regs.c)); return true; // LD A, (C)
    case 0xEA: bus_.write(fetch16(), regs.a); return true;                       // LD (nn), A
    case 0xFA: regs.a = bus_.read(fetch16()); return true;                       // LD A, (nn)
    default: return false;
    }
}

} // namespace fourshades
```

- [ ] **Step 3: Run the tests to see them pass**

```powershell
.\tools\dev.cmd cmake --build --preset release
.\build\release\tools\sst\sst_runner.exe --only "40-7f,06,0e,16,1e,26,2e,36,3e,02,12,22,32,0a,1a,2a,3a,e0,f0,e2,f2,ea,fa" --out build/sst-partial.json
.\tools\dev.cmd ctest --preset release
```

Expected: `selected: 86 / 86 passing  (0 unimplemented, …)` with no `FAIL` lines, and the unit tests passing. For any `FAIL` line, read the named test in `tools/sst/data/v1/<file>.json`, fix the code (never the test), and rerun.

- [ ] **Step 4: Full run, then update the scoreboard**

```powershell
.\build\release\tools\sst\sst_runner.exe
python tools/scoreboard.py update build/sst-results.json
python tools/check_core_isolation.py
```

Expected: `CPU instructions: 90 / 500 passing`, `scoreboard updated: cpu instructions 90 / 500`, `core isolation check passed`.

- [ ] **Step 5: Commit**

```powershell
git add src/core/CpuLoads8.cpp README.md scoreboard.json
git commit -m "feat(cpu): 8-bit loads; CPU 90/500" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 10: 8-bit arithmetic and logic (96 instructions)

**Files:**
- Modify (replace whole file): `src/core/CpuAlu8.cpp`
- Modify (generated): `README.md` scoreboard block, `scoreboard.json`

**Interfaces:**
- Consumes: Task 3 helpers, including `setFlags`.
- Produces: `Cpu::executeAlu8`, `alu8`, `inc8`, `dec8`, `daa`. `executeAlu8` returns true for exactly: 80-bf; c6 ce d6 de e6 ee f6 fe; INC r 04 0c 14 1c 24 2c 34 3c; DEC r 05 0d 15 1d 25 2d 35 3d; 07 0f 17 1f 27 2f 37 3f.

Selection: `80-bf,c6,ce,d6,de,e6,ee,f6,fe,04,0c,14,1c,24,2c,34,3c,05,0d,15,1d,25,2d,35,3d,07,0f,17,1f,27,2f,37,3f` (96 files).

- [ ] **Step 1: Run the tests to see them fail**

```powershell
.\build\release\tools\sst\sst_runner.exe --only "80-bf,c6,ce,d6,de,e6,ee,f6,fe,04,0c,14,1c,24,2c,34,3c,05,0d,15,1d,25,2d,35,3d,07,0f,17,1f,27,2f,37,3f" --out build/sst-partial.json
```

Expected: `selected: 0 / 96 passing  (96 unimplemented, …)`.

- [ ] **Step 2: Implement**

`src/core/CpuAlu8.cpp`:

```cpp
#include "core/Cpu.h"

namespace fourshades {

bool Cpu::executeAlu8(u8 opcode) {
    const int y = (opcode >> 3) & 7;
    const int z = opcode & 7;

    if (opcode >= 0x80 && opcode <= 0xBF) { // ALU A, r
        alu8(y, readR8(z));
        return true;
    }
    if (opcode >= 0xC0 && z == 6) { // ALU A, n (C6 CE D6 DE E6 EE F6 FE)
        alu8(y, fetch8());
        return true;
    }
    if (opcode < 0x40 && z == 4) { // INC r
        writeR8(y, inc8(readR8(y)));
        return true;
    }
    if (opcode < 0x40 && z == 5) { // DEC r
        writeR8(y, dec8(readR8(y)));
        return true;
    }

    const bool carry = regs.flag(Registers::FlagC);
    switch (opcode) {
    case 0x07: { // RLCA. The A-register rotates always clear Z.
        const bool out = (regs.a & 0x80) != 0;
        regs.a = static_cast<u8>((regs.a << 1) | (out ? 0x01 : 0));
        setFlags(false, false, false, out);
        return true;
    }
    case 0x0F: { // RRCA
        const bool out = (regs.a & 0x01) != 0;
        regs.a = static_cast<u8>((regs.a >> 1) | (out ? 0x80 : 0));
        setFlags(false, false, false, out);
        return true;
    }
    case 0x17: { // RLA
        const bool out = (regs.a & 0x80) != 0;
        regs.a = static_cast<u8>((regs.a << 1) | (carry ? 0x01 : 0));
        setFlags(false, false, false, out);
        return true;
    }
    case 0x1F: { // RRA
        const bool out = (regs.a & 0x01) != 0;
        regs.a = static_cast<u8>((regs.a >> 1) | (carry ? 0x80 : 0));
        setFlags(false, false, false, out);
        return true;
    }
    case 0x27: daa(); return true;
    case 0x2F: // CPL
        regs.a = static_cast<u8>(~regs.a);
        setFlags(regs.flag(Registers::FlagZ), true, true, carry);
        return true;
    case 0x37: setFlags(regs.flag(Registers::FlagZ), false, false, true); return true;   // SCF
    case 0x3F: setFlags(regs.flag(Registers::FlagZ), false, false, !carry); return true; // CCF
    default: return false;
    }
}

void Cpu::alu8(int operation, u8 value) {
    const int a = regs.a;
    const int v = value;
    const int carry = regs.flag(Registers::FlagC) ? 1 : 0;
    switch (operation) {
    case 0: { // ADD
        const int r = a + v;
        regs.a = static_cast<u8>(r);
        setFlags(regs.a == 0, false, ((a & 0xF) + (v & 0xF)) > 0xF, r > 0xFF);
        break;
    }
    case 1: { // ADC
        const int r = a + v + carry;
        regs.a = static_cast<u8>(r);
        setFlags(regs.a == 0, false, ((a & 0xF) + (v & 0xF) + carry) > 0xF, r > 0xFF);
        break;
    }
    case 2: { // SUB
        const int r = a - v;
        regs.a = static_cast<u8>(r);
        setFlags(regs.a == 0, true, (a & 0xF) < (v & 0xF), r < 0);
        break;
    }
    case 3: { // SBC
        const int r = a - v - carry;
        regs.a = static_cast<u8>(r);
        setFlags(regs.a == 0, true, ((a & 0xF) - (v & 0xF) - carry) < 0, r < 0);
        break;
    }
    case 4: // AND
        regs.a = static_cast<u8>(a & v);
        setFlags(regs.a == 0, false, true, false);
        break;
    case 5: // XOR
        regs.a = static_cast<u8>(a ^ v);
        setFlags(regs.a == 0, false, false, false);
        break;
    case 6: // OR
        regs.a = static_cast<u8>(a | v);
        setFlags(regs.a == 0, false, false, false);
        break;
    default: { // CP: SUB without storing the result
        const int r = a - v;
        setFlags(static_cast<u8>(r) == 0, true, (a & 0xF) < (v & 0xF), r < 0);
        break;
    }
    }
}

u8 Cpu::inc8(u8 value) {
    const u8 r = static_cast<u8>(value + 1);
    setFlags(r == 0, false, (value & 0xF) == 0xF, regs.flag(Registers::FlagC));
    return r;
}

u8 Cpu::dec8(u8 value) {
    const u8 r = static_cast<u8>(value - 1);
    setFlags(r == 0, true, (value & 0xF) == 0, regs.flag(Registers::FlagC));
    return r;
}

// Adjusts A to binary-coded decimal after an addition (N clear) or a
// subtraction (N set), using the H and C flags that operation left.
void Cpu::daa() {
    u8 a = regs.a;
    bool carry = regs.flag(Registers::FlagC);
    const bool half = regs.flag(Registers::FlagH);
    const bool subtract = regs.flag(Registers::FlagN);
    if (!subtract) {
        if (carry || a > 0x99) {
            a = static_cast<u8>(a + 0x60);
            carry = true;
        }
        if (half || (a & 0x0F) > 0x09) {
            a = static_cast<u8>(a + 0x06);
        }
    } else {
        if (carry) {
            a = static_cast<u8>(a - 0x60);
        }
        if (half) {
            a = static_cast<u8>(a - 0x06);
        }
    }
    regs.a = a;
    setFlags(a == 0, subtract, false, carry);
}

} // namespace fourshades
```

- [ ] **Step 3: Run the tests to see them pass**

```powershell
.\tools\dev.cmd cmake --build --preset release
.\build\release\tools\sst\sst_runner.exe --only "80-bf,c6,ce,d6,de,e6,ee,f6,fe,04,0c,14,1c,24,2c,34,3c,05,0d,15,1d,25,2d,35,3d,07,0f,17,1f,27,2f,37,3f" --out build/sst-partial.json
.\tools\dev.cmd ctest --preset release
```

Expected: `selected: 96 / 96 passing` with no `FAIL` lines, and the unit tests passing. DAA (`27`) is the most likely to fail. If it does, check the flag rules against Pan Docs' DAA entry (https://gbdev.io/pandocs/CPU_Instruction_Set.html) and fix the code.

- [ ] **Step 4: Full run, then update the scoreboard**

```powershell
.\build\release\tools\sst\sst_runner.exe
python tools/scoreboard.py update build/sst-results.json
python tools/check_core_isolation.py
```

Expected: `CPU instructions: 186 / 500 passing`.

- [ ] **Step 5: Commit**

```powershell
git add src/core/CpuAlu8.cpp README.md scoreboard.json
git commit -m "feat(cpu): 8-bit arithmetic, logic and A rotates; CPU 186/500" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 11: 16-bit loads, stack and arithmetic (28 instructions)

**Files:**
- Modify (replace whole file): `src/core/CpuWide.cpp`
- Modify (generated): `README.md` scoreboard block, `scoreboard.json`

**Interfaces:**
- Consumes: `readRp`, `writeRp`, `readRp2`, `writeRp2`, `push16`, `pop16`, `setFlags` (Task 3).
- Produces: `Cpu::executeWide`, `addHl`, `addSpOffset`. `executeWide` returns true for exactly: 01 11 21 31; 03 13 23 33; 0b 1b 2b 3b; 09 19 29 39; c1 d1 e1 f1; c5 d5 e5 f5; 08 f9 e8 f8.

Selection: `01,11,21,31,03,13,23,33,0b,1b,2b,3b,09,19,29,39,c1,d1,e1,f1,c5,d5,e5,f5,08,f9,e8,f8` (28 files).

- [ ] **Step 1: Run the tests to see them fail**

```powershell
.\build\release\tools\sst\sst_runner.exe --only "01,11,21,31,03,13,23,33,0b,1b,2b,3b,09,19,29,39,c1,d1,e1,f1,c5,d5,e5,f5,08,f9,e8,f8" --out build/sst-partial.json
```

Expected: `selected: 0 / 28 passing  (28 unimplemented, …)`.

- [ ] **Step 2: Implement**

`src/core/CpuWide.cpp`:

```cpp
#include "core/Cpu.h"

namespace fourshades {

bool Cpu::executeWide(u8 opcode) {
    const int p = (opcode >> 4) & 3;

    if (opcode < 0x40) {
        switch (opcode & 0x0F) {
        case 0x1: // LD rr, nn
            writeRp(p, fetch16());
            return true;
        case 0x3: // INC rr: the 16-bit increment costs an internal cycle
            writeRp(p, static_cast<u16>(readRp(p) + 1));
            bus_.idle();
            return true;
        case 0xB: // DEC rr
            writeRp(p, static_cast<u16>(readRp(p) - 1));
            bus_.idle();
            return true;
        case 0x9: // ADD HL, rr
            addHl(readRp(p));
            bus_.idle();
            return true;
        default:
            break;
        }
    }
    if (opcode >= 0xC0) {
        switch (opcode & 0x0F) {
        case 0x1: // POP rr (C1 D1 E1 F1; F1 is POP AF, which masks F's low nibble)
            writeRp2(p, pop16());
            return true;
        case 0x5: // PUSH rr
            push16(readRp2(p));
            return true;
        default:
            break;
        }
    }

    switch (opcode) {
    case 0x08: { // LD (nn), SP
        const u16 address = fetch16();
        bus_.write(address, lo(regs.sp));
        bus_.write(static_cast<u16>(address + 1), hi(regs.sp));
        return true;
    }
    case 0xF9: // LD SP, HL
        regs.sp = regs.hl();
        bus_.idle();
        return true;
    case 0xE8: { // ADD SP, e
        const u16 result = addSpOffset(fetch8());
        bus_.idle();
        bus_.idle();
        regs.sp = result;
        return true;
    }
    case 0xF8: { // LD HL, SP+e
        const u16 result = addSpOffset(fetch8());
        bus_.idle();
        regs.setHl(result);
        return true;
    }
    default:
        return false;
    }
}

void Cpu::addHl(u16 value) {
    const int hl = regs.hl();
    const int r = hl + value;
    setFlags(regs.flag(Registers::FlagZ), false, ((hl & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF, r > 0xFFFF);
    regs.setHl(static_cast<u16>(r));
}

// SP plus a signed offset. The flags come from the unsigned low-byte addition,
// even though the offset itself is signed; Z and N are always cleared.
u16 Cpu::addSpOffset(u8 offset) {
    const int sp = regs.sp;
    setFlags(false, false, ((sp & 0x0F) + (offset & 0x0F)) > 0x0F, ((sp & 0xFF) + offset) > 0xFF);
    return static_cast<u16>(sp + static_cast<i8>(offset));
}

} // namespace fourshades
```

- [ ] **Step 3: Run the tests to see them pass**

```powershell
.\tools\dev.cmd cmake --build --preset release
.\build\release\tools\sst\sst_runner.exe --only "01,11,21,31,03,13,23,33,0b,1b,2b,3b,09,19,29,39,c1,d1,e1,f1,c5,d5,e5,f5,08,f9,e8,f8" --out build/sst-partial.json
.\tools\dev.cmd ctest --preset release
```

Expected: `selected: 28 / 28 passing` with no `FAIL` lines, and the unit tests passing.

- [ ] **Step 4: Full run, then update the scoreboard**

```powershell
.\build\release\tools\sst\sst_runner.exe
python tools/scoreboard.py update build/sst-results.json
python tools/check_core_isolation.py
```

Expected: `CPU instructions: 214 / 500 passing`.

- [ ] **Step 5: Commit**

```powershell
git add src/core/CpuWide.cpp README.md scoreboard.json
git commit -m "feat(cpu): 16-bit loads, stack and arithmetic; CPU 214/500" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 12: Jumps, calls, returns and RST (30 instructions)

**Files:**
- Modify (replace whole file): `src/core/CpuControl.cpp`
- Modify (generated): `README.md` scoreboard block, `scoreboard.json`

**Interfaces:**
- Consumes: `fetch8`, `fetch16`, `push16`, `pop16`, `condition` (Task 3).
- Produces: `Cpu::executeControl`, `jumpRelative`. `executeControl` returns true for exactly: 18 20 28 30 38; c3 c2 ca d2 da; e9; cd c4 cc d4 dc; c9 d9 c0 c8 d0 d8; c7 cf d7 df e7 ef f7 ff.

Selection: `18,20,28,30,38,c3,c2,ca,d2,da,e9,cd,c4,cc,d4,dc,c9,d9,c0,c8,d0,d8,c7,cf,d7,df,e7,ef,f7,ff` (30 files).

- [ ] **Step 1: Run the tests to see them fail**

```powershell
.\build\release\tools\sst\sst_runner.exe --only "18,20,28,30,38,c3,c2,ca,d2,da,e9,cd,c4,cc,d4,dc,c9,d9,c0,c8,d0,d8,c7,cf,d7,df,e7,ef,f7,ff" --out build/sst-partial.json
```

Expected: `selected: 0 / 30 passing  (30 unimplemented, …)`.

- [ ] **Step 2: Implement**

`src/core/CpuControl.cpp`:

```cpp
#include "core/Cpu.h"

namespace fourshades {

bool Cpu::executeControl(u8 opcode) {
    const int y = (opcode >> 3) & 7;

    switch (opcode) {
    case 0x18: { // JR e
        const u8 offset = fetch8();
        jumpRelative(offset);
        return true;
    }
    case 0x20: case 0x28: case 0x30: case 0x38: { // JR cc, e (cc = y - 4)
        const u8 offset = fetch8();
        if (condition(y - 4)) {
            jumpRelative(offset);
        }
        return true;
    }
    case 0xC3: { // JP nn
        const u16 target = fetch16();
        regs.pc = target;
        bus_.idle();
        return true;
    }
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: { // JP cc, nn
        const u16 target = fetch16();
        if (condition(y)) {
            regs.pc = target;
            bus_.idle();
        }
        return true;
    }
    case 0xE9: // JP HL: no extra cycle
        regs.pc = regs.hl();
        return true;
    case 0xCD: { // CALL nn
        const u16 target = fetch16();
        push16(regs.pc);
        regs.pc = target;
        return true;
    }
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: { // CALL cc, nn
        const u16 target = fetch16();
        if (condition(y)) {
            push16(regs.pc);
            regs.pc = target;
        }
        return true;
    }
    case 0xC9: // RET
        regs.pc = pop16();
        bus_.idle();
        return true;
    case 0xD9: // RETI: like RET, and enables interrupts with no delay
        regs.pc = pop16();
        bus_.idle();
        ime = true;
        return true;
    case 0xC0: case 0xC8: case 0xD0: case 0xD8: // RET cc: checking cc costs a cycle
        bus_.idle();
        if (condition(y)) {
            regs.pc = pop16();
            bus_.idle();
        }
        return true;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF: // RST
        push16(regs.pc);
        regs.pc = static_cast<u16>(y * 8);
        return true;
    default:
        return false;
    }
}

void Cpu::jumpRelative(u8 offset) {
    regs.pc = static_cast<u16>(regs.pc + static_cast<i8>(offset));
    bus_.idle();
}

} // namespace fourshades
```

- [ ] **Step 3: Run the tests to see them pass**

```powershell
.\tools\dev.cmd cmake --build --preset release
.\build\release\tools\sst\sst_runner.exe --only "18,20,28,30,38,c3,c2,ca,d2,da,e9,cd,c4,cc,d4,dc,c9,d9,c0,c8,d0,d8,c7,cf,d7,df,e7,ef,f7,ff" --out build/sst-partial.json
.\tools\dev.cmd ctest --preset release
```

Expected: `selected: 30 / 30 passing` with no `FAIL` lines, and the unit tests passing.

- [ ] **Step 4: Full run, then update the scoreboard**

```powershell
.\build\release\tools\sst\sst_runner.exe
python tools/scoreboard.py update build/sst-results.json
python tools/check_core_isolation.py
```

Expected: `CPU instructions: 244 / 500 passing`. Every base opcode now passes. Only the 256 CB files remain, all `unimplemented`.

- [ ] **Step 5: Commit**

```powershell
git add src/core/CpuControl.cpp README.md scoreboard.json
git commit -m "feat(cpu): jumps, calls, returns and RST; CPU 244/500" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 13: CB-prefixed instructions (256 instructions)

**Files:**
- Modify (replace whole file): `src/core/CpuCb.cpp`
- Modify: `tests/test_sst_run.cpp` (delete the `"an unimplemented opcode is reported, not failed"` test case: once every opcode exists, nothing can reach the unimplemented path)
- Modify (generated): `README.md` scoreboard block, `scoreboard.json`

**Interfaces:**
- Consumes: `fetch8`, `readR8`, `writeR8`, `setFlags` (Task 3).
- Produces: `Cpu::executeCb`, `rotateShift`. Every legal opcode is now implemented.

- [ ] **Step 1: Run the tests to see them fail**

```powershell
.\build\release\tools\sst\sst_runner.exe --only cb00-cbff --out build/sst-partial.json
```

Expected: `selected: 0 / 256 passing  (256 unimplemented, …)`.

- [ ] **Step 2: Implement**

`src/core/CpuCb.cpp`:

```cpp
#include "core/Cpu.h"

namespace fourshades {

// CB xx: x = 0 rotate/shift/swap, 1 BIT, 2 RES, 3 SET; y = operation or bit;
// z = register (6 is (HL), which reads memory and, except for BIT, writes it back).
void Cpu::executeCb() {
    const u8 opcode = fetch8();
    const int x = opcode >> 6;
    const int y = (opcode >> 3) & 7;
    const int z = opcode & 7;
    const u8 value = readR8(z);

    switch (x) {
    case 0:
        writeR8(z, rotateShift(y, value));
        break;
    case 1: // BIT y, r
        setFlags((value & (1 << y)) == 0, false, true, regs.flag(Registers::FlagC));
        break;
    case 2: // RES y, r
        writeR8(z, static_cast<u8>(value & ~(1 << y)));
        break;
    default: // SET y, r
        writeR8(z, static_cast<u8>(value | (1 << y)));
        break;
    }
}

u8 Cpu::rotateShift(int operation, u8 value) {
    const bool oldCarry = regs.flag(Registers::FlagC);
    u8 r = 0;
    bool carry = false;
    switch (operation) {
    case 0: // RLC
        carry = (value & 0x80) != 0;
        r = static_cast<u8>((value << 1) | (carry ? 0x01 : 0));
        break;
    case 1: // RRC
        carry = (value & 0x01) != 0;
        r = static_cast<u8>((value >> 1) | (carry ? 0x80 : 0));
        break;
    case 2: // RL
        carry = (value & 0x80) != 0;
        r = static_cast<u8>((value << 1) | (oldCarry ? 0x01 : 0));
        break;
    case 3: // RR
        carry = (value & 0x01) != 0;
        r = static_cast<u8>((value >> 1) | (oldCarry ? 0x80 : 0));
        break;
    case 4: // SLA
        carry = (value & 0x80) != 0;
        r = static_cast<u8>(value << 1);
        break;
    case 5: // SRA: keeps bit 7
        carry = (value & 0x01) != 0;
        r = static_cast<u8>((value >> 1) | (value & 0x80));
        break;
    case 6: // SWAP
        r = static_cast<u8>((value << 4) | (value >> 4));
        break;
    default: // SRL
        carry = (value & 0x01) != 0;
        r = static_cast<u8>(value >> 1);
        break;
    }
    setFlags(r == 0, false, false, carry);
    return r;
}

} // namespace fourshades
```

In `tests/test_sst_run.cpp`, delete the whole `TEST_CASE("an unimplemented opcode is reported, not failed") { ... }` block, including its comment.

- [ ] **Step 3: Run the tests to see them pass**

```powershell
.\tools\dev.cmd cmake --build --preset release
.\build\release\tools\sst\sst_runner.exe --only cb00-cbff --out build/sst-partial.json
.\tools\dev.cmd ctest --preset release
```

Expected: `selected: 256 / 256 passing` with no `FAIL` lines, and the unit tests passing.

- [ ] **Step 4: Full run, then update the scoreboard**

```powershell
.\build\release\tools\sst\sst_runner.exe
python tools/scoreboard.py update build/sst-results.json
python tools/check_core_isolation.py
```

Expected: `CPU instructions: 500 / 500 passing  (0 unimplemented, …)`.

- [ ] **Step 5: Commit**

```powershell
git add src/core/CpuCb.cpp tests/test_sst_run.cpp README.md scoreboard.json
git commit -m "feat(cpu): CB-prefixed instructions; CPU 500/500" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 14: Check HALT and STOP against Pan Docs, verify every success criterion, publish

**Files:**
- Modify (only if Pan Docs disagrees): `src/core/Cpu.cpp` (the `0x10`/`0x76` cases), `docs/known-divergences.md`
- Modify (generated, if the score changes): `README.md` scoreboard block, `scoreboard.json`

- [ ] **Step 1: Compare HALT and STOP with Pan Docs**

Read these two pages in full:
- https://gbdev.io/pandocs/halt.html
- https://gbdev.io/pandocs/Reducing_Power_Consumption.html (the STOP section and its flowchart)

The CPU currently does this, and the tests expect it:
- **HALT (76):** one fetch cycle. PC advances by 1 and the CPU enters Halted, then idles every M-cycle until an interrupt (piece 2).
- **STOP (10):** one fetch cycle. PC advances by 1 and the CPU enters Stopped, then idles.

For each instruction, answer from Pan Docs for the case these tests model: a DMG, no joypad button held, no interrupt pending, IME as given.
1. How many bytes does the instruction occupy, i.e. how far does PC advance?
2. Is there a bus read of the following byte?
3. Does the CPU enter HALT/STOP mode?

If Pan Docs agrees with the current behaviour for both, change nothing in code and replace `_None recorded yet._` in `docs/known-divergences.md` with:

```markdown
_None recorded yet._ HALT (76) and STOP (10) were checked against Pan Docs on
<date> for the no-button, no-pending-interrupt case the tests model; both match.
```

If Pan Docs disagrees (for example, it says STOP is 2 bytes in this case), change the code to follow Pan Docs. If Pan Docs says the second byte is skipped with no read, advance `regs.pc` by one more in the `0x10` case without calling `fetch8()`. If it says the byte is read, call `fetch8()`. Then add an entry to `docs/known-divergences.md`:

```markdown
## STOP (0x10): <one-line summary>

- **Test:** SingleStepTests `v1/10.json` expects <what the test expects, e.g. PC + 1, cycles r-m, ---, --->.
- **Pan Docs:** <quote the relevant sentence>, <link to the section>.
- **FourShades:** follows Pan Docs, so `10` fails all 1,000 tests on `<field>`.
- **Checked:** <date>.
```

Then rebuild and run the full suite, and confirm the only failing file is the one you recorded:

```powershell
.\tools\dev.cmd cmake --build --preset release
.\build\release\tools\sst\sst_runner.exe
```

- [ ] **Step 2: Check every success criterion from the spec**

```powershell
.\tools\dev.cmd ctest --preset release
python -m unittest discover -s tools -p "test_*.py"
python tools/check_core_isolation.py
.\build\release\tools\sst\sst_runner.exe
python tools/scoreboard.py update build/sst-results.json
python tools/scoreboard.py check build/sst-results.json
```

Record the actual output. The criteria:
1. The score is 500 / 500, or 500 minus each recorded divergence.
2. `elapsed_seconds` in `build/sst-results.json` is under 60.
3. The unit tests, the Python tests and the isolation check pass, and `scoreboard.py check` says it matches.

If the run takes 60 seconds or more, profile before changing anything. JSON parsing is the likely cost. Report the measurement rather than tuning blind.

- [ ] **Step 3: Prove a hand-edited scoreboard fails the check**

```powershell
(Get-Content README.md -Raw) -replace '(\d+) / 500', '499 / 500' | Set-Content README.md -NoNewline
python tools/scoreboard.py check build/sst-results.json; "exit code: $LASTEXITCODE"
git checkout -- README.md
python tools/scoreboard.py check build/sst-results.json
```

Expected: `scoreboard does not match the test results: README.md scoreboard block` and `exit code: 1`. After the restore, `scoreboard matches the test results`. If the real score is itself 499, use `498` in the edit instead.

- [ ] **Step 4: Commit and push**

```powershell
git add docs/known-divergences.md README.md scoreboard.json src/core/Cpu.cpp
git status --short
git commit -m "docs: check HALT/STOP against Pan Docs; piece 1 complete" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
git push origin main
```

Only stage files that actually changed: drop `src/core/Cpu.cpp` from the `git add` if Step 1 changed no code. Check `git status --short` before committing.

- [ ] **Step 5: Verify CI is green on the pushed commit**

```powershell
$sha = git rev-parse HEAD
(Invoke-RestMethod "https://api.github.com/repos/doozleb/FourShades/commits/$sha/check-runs").check_runs | Select-Object name, status, conclusion
```

Expected: `build-and-score  completed  success`. Piece 1 is done only when this is green.
