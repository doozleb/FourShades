"""
Fail if the emulator core depends on the test harness or on test data.

The core may include only its own headers (core/...) and standard headers, and
may not mention files, JSON or the test suite at all. That keeps it
impossible, not just discouraged, for core code to look at the tests or to
know what a passing result looks like.

It may not name an individual test ROM either, in code or in a comment. A
comment naming the ROM that pins a behaviour reads like evidence while being
unverifiable from the core, and it ties the core's prose to a test list that
is regenerated from outside it: the divergences file is where a ROM's name,
its provenance and its figures belong, and a comment should cite the entry.
The word list below cannot see a name like m3_bgp_change, which is how the
convention drifted through a whole piece of work before anyone noticed, so
ROM_NAME catches the shapes those names take.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORE = ROOT / "src" / "core"
FORBIDDEN = re.compile(
    r"\b(sst|json|fopen|ifstream|ofstream|fstream|singlesteptests"
    r"|blargg|mooneye|shootout|passed|failed|fibonacci)\b",
    re.IGNORECASE)
# The byte signature one test suite uses to report results, in any spelling.
SIGNATURE = re.compile(r"(0x)?de[\s,_]*(0x)?b0[\s,_]*(0x)?61", re.IGNORECASE)
# The shapes a test ROM's name takes in the suites this project scores
# against: a lower-case name with two or more underscores (m3_bgp_change,
# intr_2_mode0_timing_sprites, stat_lyc_onoff), the same kind of name with a
# hardware-model suffix (lcdon_timing-GS, boot_div-dmgABCmgb), or a file name
# ending in .gb or .gbc. The core's own identifiers are camelCase, so none of
# these match anything it legitimately says; a name with fewer underscores and
# no suffix (sprite_priority, bully) still gets past, so this narrows the gap
# rather than closing it.
ROM_NAME = re.compile(
    r"\b[a-z][a-z0-9]*(?:_[a-z0-9]+){2,}\b"
    r"|\b[a-z][a-z0-9]*(?:_[a-z0-9]+)+-[A-Za-z]"
    r"|\b[\w-]+\.gbc?\b")
INCLUDE = re.compile(r'#\s*include\s*[<"]([^">]+)[">]')


def main():
    problems = []
    for path in sorted(CORE.rglob("*")):
        if path.suffix not in (".h", ".hpp", ".cpp"):
            continue
        for number, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            where = f"{path.relative_to(ROOT).as_posix()}:{number}"
            if FORBIDDEN.search(text) or SIGNATURE.search(text):
                problems.append(f"{where}: forbidden word: {text.strip()}")
            rom = ROM_NAME.search(text)
            if rom:
                problems.append(
                    f"{where}: test-suite name ({rom.group(0)}): cite the"
                    f" docs/known-divergences.md entry, not the ROM: {text.strip()}")
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
