"""
Generate the README scoreboard, the per-group table and scoreboard.json from
the SingleStepTests and test-ROM results files.

    python tools/scoreboard.py update build/sst-results.json build/rom-results.json
    python tools/scoreboard.py check  build/sst-results.json build/rom-results.json

`check` exits 1 if the committed README blocks or scoreboard.json differ from
what the results say. CI runs it, so a hand-edited score fails the build,
whether it's too high or too low.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
START, END = "<!-- scoreboard:start -->", "<!-- scoreboard:end -->"
GROUPS_START, GROUPS_END = "<!-- groups:start -->", "<!-- groups:end -->"
BAR = 16
CPU_FILES = 500
# The Shootout's 167 DMG tests, less the 2 it treats as informational (no pass
# condition); see docs/superpowers/specs/2026-09-11-machine-test-roms-design.md
ROM_TESTS = 165
INFORMATIONAL_TESTS = 2


def load_sst(path):
    results = json.loads(Path(path).read_text(encoding="utf-8"))
    if results.get("partial"):
        raise SystemExit("error: partial SingleStepTests results (--only was used); run the full suite")
    files = results.get("files", [])
    if results.get("total_files") != CPU_FILES or len(files) != CPU_FILES:
        raise SystemExit(f"error: expected SingleStepTests results for {CPU_FILES} files")
    if sum(1 for f in files if f["status"] == "pass") != results.get("passing_files"):
        raise SystemExit("error: SingleStepTests results are inconsistent")
    return results


def load_roms(path):
    results = json.loads(Path(path).read_text(encoding="utf-8"))
    if results.get("partial"):
        raise SystemExit("error: partial test-ROM results (--only was used); run all tests")
    tests = results.get("tests", [])
    scored = [t for t in tests if t["status"] != "informational"]
    informational = [t for t in tests if t["status"] == "informational"]
    if results.get("total") != ROM_TESTS or len(scored) != ROM_TESTS:
        raise SystemExit(f"error: expected test-ROM results for {ROM_TESTS} scored tests")
    if len(informational) != INFORMATIONAL_TESTS:
        raise SystemExit(f"error: expected {INFORMATIONAL_TESTS} informational tests, found {len(informational)}")
    if sum(1 for t in scored if t["status"] == "pass") != results.get("passing"):
        raise SystemExit("error: test-ROM results are inconsistent")
    return results


def informational_of(rom):
    return [t["name"] for t in rom["tests"] if t["status"] == "informational"]


def groups_of(rom):
    order, groups = [], {}
    for t in rom["tests"]:
        if t["status"] == "informational":
            continue
        g = groups.get(t["group"])
        if g is None:
            g = groups[t["group"]] = {"group": t["group"], "passing": 0, "total": 0, "first_failure": None}
            order.append(t["group"])
        g["total"] += 1
        if t["status"] == "pass":
            g["passing"] += 1
        elif g["first_failure"] is None:
            g["first_failure"] = {"test": t["name"], "reason": t["reason"]}
    return [groups[name] for name in order]


def scoreboard(sst, rom):
    return {
        "cpu_instructions": {"passing": sst["passing_files"], "total": CPU_FILES},
        "test_roms": {"passing": rom["passing"], "total": ROM_TESTS, "groups": groups_of(rom),
                      "informational": informational_of(rom)},
        "source": {"sst_suite": sst["suite"], "sst_commit": sst["commit"],
                   "rom_suite": rom["suite"], "shootout_commit": rom["shootout_commit"]},
    }


def line(label, passing, total):
    filled = BAR * passing // total
    return f"{label:<18}{'█' * filled}{'░' * (BAR - filled)}  {passing:>4} / {total}"


def score_block(board):
    cpu, roms = board["cpu_instructions"], board["test_roms"]
    return "\n".join([START, "```", line("cpu instructions", cpu["passing"], cpu["total"]),
                      line("test roms", roms["passing"], roms["total"]), "```", END])


def groups_block(board):
    rows = [GROUPS_START, "| group | passing | first failing test |", "|---|---|---|"]
    for g in board["test_roms"]["groups"]:
        failure = g["first_failure"]
        cell = "" if failure is None else f"`{failure['test']}`: {failure['reason']}".replace("|", "/")
        rows.append(f"| {g['group']} | {g['passing']} / {g['total']} | {cell} |")
    names = ", ".join(f"`{name}`" for name in board["test_roms"]["informational"])
    # The blank line ends the table; without it GitHub renders the note as a row.
    rows += ["", f"Not counted (informational in the Shootout, no pass condition): {names}.", GROUPS_END]
    return "\n".join(rows)


def replace_block(text, start_marker, end_marker, block):
    start, end = text.find(start_marker), text.find(end_marker)
    if start == -1 or end == -1 or end < start:
        raise SystemExit(f"error: README has no {start_marker} ... {end_marker} markers")
    return text[:start] + block + text[end + len(end_marker):]


def run(mode, sst_path, rom_path, readme_path, board_path):
    board = scoreboard(load_sst(sst_path), load_roms(rom_path))
    board_json = json.dumps(board, indent=2) + "\n"
    readme = Path(readme_path).read_text(encoding="utf-8")
    new_readme = replace_block(readme, START, END, score_block(board))
    new_readme = replace_block(new_readme, GROUPS_START, GROUPS_END, groups_block(board))

    if mode == "update":
        Path(readme_path).write_text(new_readme, encoding="utf-8")
        Path(board_path).write_text(board_json, encoding="utf-8")
        print(f"scoreboard updated: cpu instructions {board['cpu_instructions']['passing']} / {CPU_FILES}, "
              f"test roms {board['test_roms']['passing']} / {ROM_TESTS}")
        return 0

    problems = []
    if new_readme != readme:
        problems.append("README.md scoreboard or group table")
    board_file = Path(board_path)
    if not board_file.exists() or board_file.read_text(encoding="utf-8") != board_json:
        problems.append("scoreboard.json")
    if problems:
        print("scoreboard does not match the test results: " + ", ".join(problems))
        print("run: python tools/scoreboard.py update build/sst-results.json build/rom-results.json")
        return 1
    print("scoreboard matches the test results")
    return 0


def main(argv):
    if len(argv) != 4 or argv[1] not in ("update", "check"):
        raise SystemExit(__doc__)
    return run(argv[1], argv[2], argv[3], ROOT / "README.md", ROOT / "scoreboard.json")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
