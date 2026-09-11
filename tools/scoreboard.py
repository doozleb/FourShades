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
