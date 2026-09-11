"""
Fail if the emulator core depends on the test harness or on test data.

The core may include only its own headers (core/...) and standard headers, and
may not mention files, JSON or the test suite at all. That keeps it
impossible, not just discouraged, for core code to look at the tests or to
know what a passing result looks like.
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
