"""
Build tools/roms/tests.json from the Emulator Shootout's own test definitions,
at a pinned commit, so anyone can regenerate the list and diff it.

    python tools/roms/make_test_list.py          # write tests.json
    python tools/roms/make_test_list.py --check  # exit 1 if tests.json differs

Only the Shootout's active original-Game-Boy (DMG) tests are kept: no
model=CGB or model=SGB. The count must come out at exactly 167.
"""

import ast
import json
import sys
import time
import urllib.request
from pathlib import Path

REPO = "gbdev/GBEmulatorShootout"
COMMIT = "38b926bdbc26993d1b4c43e97979ecc66287bf02"
RAW = f"https://raw.githubusercontent.com/{REPO}/{COMMIT}/"
TREE = f"https://api.github.com/repos/{REPO}/git/trees/{COMMIT}?recursive=1"
SUITES = ["blargg", "mooneye", "mealybug", "acid", "ashiepaws", "cpp", "daid"]
EXPECTED = 167
OUT = Path(__file__).resolve().parent / "tests.json"

# First match wins, so the specific mooneye groups come before the catch-all.
GROUPS = [
    (("blargg/cpu_instrs/",), "cpu instructions"),
    (("blargg/instr_timing", "blargg/mem_timing", "blargg/halt_bug"), "cpu timing"),
    (("blargg/oam_bug/",), "oam bug"),
    (("blargg/dmg_sound/",), "sound"),
    (("mooneye/acceptance/timer/",), "timer"),
    (("mooneye/acceptance/boot_",), "boot state"),
    (("mooneye/acceptance/oam_dma",), "oam dma"),
    (("mooneye/acceptance/serial/",), "serial"),
    (("mooneye/acceptance/ppu/",), "ppu timing"),
    (("mooneye/emulator-only/mbc1/",), "mbc1"),
    (("mooneye/emulator-only/mbc2/", "mooneye/emulator-only/mbc5/"), "mbc2 / mbc5"),
    (("mooneye/acceptance/",), "cpu & interrupts"),
    (("cpp/",), "mbc3 / rtc"),
    (("mooneye/manual-only/", "acid/", "ashiepaws/", "daid/", "mealybug-tearoom-tests/"), "screen"),
]


def get(url: str) -> bytes:
    last_error = None
    for attempt in range(1, 7):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return response.read()
        except OSError as error:
            last_error = error
            time.sleep(2 * attempt)
    raise SystemExit(f"error: {url}: {last_error}")


def group_for(rom: str) -> str:
    for prefixes, group in GROUPS:
        if rom.startswith(prefixes):
            return group
    raise SystemExit(f"error: no group for {rom}")


def method_for(suite: str, rom: str) -> str:
    if suite == "blargg":
        return "blargg"
    if suite == "mooneye" and not rom.startswith("mooneye/manual-only/"):
        return "mooneye"
    return "screenshot"


def const(node):
    return node.value if isinstance(node, ast.Constant) else None


def parse_suite(suite: str, source: str) -> list[dict]:
    calls = [n for n in ast.walk(ast.parse(source)) if isinstance(n, ast.Call) and isinstance(n.func, ast.Name)]
    calls.sort(key=lambda n: (n.lineno, n.col_offset))
    tests = []
    for call in calls:
        kw = {k.arg: k.value for k in call.keywords}
        if call.func.id == "Test":
            # Test(...) calls inside helper functions have a computed name; skip them.
            if not call.args or not isinstance(call.args[0], ast.Constant):
                continue
            model = kw["model"].id if "model" in kw else "DMG"
            if model != "DMG":
                continue
            name = call.args[0].value
            rom = const(kw["rom"]) if "rom" in kw else name
            runtime = float(const(kw["runtime"]))
            if "result" in kw:
                result = kw["result"]
                refs = [const(e) for e in result.elts] if isinstance(result, ast.List) else [const(result)]
            else:
                refs = [rom.rsplit(".", 1)[0] + ".png"]
        elif call.func.id == "dmg":  # mealybug.py's DMG helper
            arg = call.args[0].value
            name = f"mealybug-tearoom-tests/{arg} (DMG)"
            rom = f"mealybug-tearoom-tests/{arg}"
            runtime = 0.5
            refs = ["mealybug-tearoom-tests/" + arg.replace(".gb", "_dmg_blob.png")]
        else:
            continue
        tests.append({"suite": suite, "name": name, "rom": rom, "runtime": runtime, "refs": refs})
    return tests


def build() -> str:
    tree = json.loads(get(TREE))
    if tree.get("truncated"):
        raise SystemExit("error: GitHub truncated the file listing")
    exists = {e["path"][len("testroms/"):] for e in tree["tree"]
              if e["type"] == "blob" and e["path"].startswith("testroms/")}
    entries = []
    for suite in SUITES:
        for t in parse_suite(suite, get(f"{RAW}testroms/{suite}.py").decode("utf-8")):
            if t["rom"] not in exists:
                raise SystemExit(f"error: {t['rom']} is not in the Shootout at {COMMIT}")
            entries.append({
                "name": t["name"],
                "rom": t["rom"],
                "group": group_for(t["rom"]),
                "method": method_for(suite, t["rom"]),
                "runtime": t["runtime"],
                "limit_seconds": max(2 * t["runtime"], t["runtime"] + 5),
                "references": [r for r in t["refs"] if r in exists],
            })
    if len(entries) != EXPECTED:
        raise SystemExit(f"error: expected {EXPECTED} DMG tests, found {len(entries)}")
    return json.dumps({"shootout_commit": COMMIT, "tests": entries}, indent=1) + "\n"


def main() -> int:
    text = build()
    if "--check" in sys.argv[1:]:
        same = OUT.exists() and OUT.read_text(encoding="utf-8") == text
        print("tests.json matches the Shootout" if same else "tests.json differs from the Shootout")
        return 0 if same else 1
    OUT.write_text(text, encoding="utf-8", newline="\n")
    counts: dict[str, int] = {}
    for t in json.loads(text)["tests"]:
        counts[t["group"]] = counts.get(t["group"], 0) + 1
    print(f"wrote {OUT} ({sum(counts.values())} tests)")
    for group, n in counts.items():
        print(f"  {n:3}  {group}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
