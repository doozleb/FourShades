import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scoreboard  # noqa: E402


def sst_results(passing, partial=False, total=500):
    files = [{"name": f"{i:03}", "status": "pass" if i < passing else "fail"} for i in range(total)]
    return {"suite": "SingleStepTests/sm83", "commit": "abc", "partial": partial,
            "total_files": total, "passing_files": passing, "files": files}


def rom_results(passing_by_group, partial=False):
    tests = []
    for group, (passing, total) in passing_by_group.items():
        for i in range(total):
            ok = i < passing
            tests.append({"name": f"{group}/t{i}.gb", "group": group, "method": "mooneye",
                          "status": "pass" if ok else "fail",
                          "reason": "Fibonacci registers at LD B,B" if ok else "timeout after 6.5 s",
                          "emulated_seconds": 1.0, "serial": ""})
    n = sum(1 for t in tests if t["status"] == "pass")
    return {"suite": "GBEmulatorShootout (DMG)", "shootout_commit": "def", "partial": partial,
            "total": len(tests), "passing": n, "elapsed_seconds": 1.0, "tests": tests}


FULL = {"timer": (3, 13), "screen": (0, 32), "cpu instructions": (11, 11), "other": (0, 111)}  # 167 in all


class ScoreboardTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.readme = self.dir / "README.md"
        self.board = self.dir / "scoreboard.json"
        self.sst = self.dir / "sst.json"
        self.rom = self.dir / "rom.json"
        self.readme.write_text(
            "intro\n<!-- scoreboard:start -->\nold\n<!-- scoreboard:end -->\nmiddle\n"
            "<!-- groups:start -->\nold\n<!-- groups:end -->\noutro\n", encoding="utf-8")

    def write(self, sst, rom):
        self.sst.write_text(json.dumps(sst), encoding="utf-8")
        self.rom.write_text(json.dumps(rom), encoding="utf-8")

    def run_mode(self, mode):
        return scoreboard.run(mode, self.sst, self.rom, self.readme, self.board)

    def test_line_format(self):
        self.assertEqual(scoreboard.line("cpu instructions", 250, 500),
                         "cpu instructions  " + "█" * 8 + "░" * 8 + "   250 / 500")
        self.assertEqual(scoreboard.line("test roms", 14, 167),
                         "test roms         " + "█" * 1 + "░" * 15 + "    14 / 167")

    def test_update_then_check_passes(self):
        self.write(sst_results(499), rom_results(FULL))
        self.assertEqual(self.run_mode("update"), 0)
        self.assertEqual(self.run_mode("check"), 0)
        text = self.readme.read_text(encoding="utf-8")
        self.assertIn("499 / 500", text)
        self.assertIn("14 / 167", text)
        self.assertIn("| timer | 3 / 13 | `timer/t3.gb`: timeout after 6.5 s |", text)
        self.assertIn("| cpu instructions | 11 / 11 |  |", text)
        self.assertTrue(text.startswith("intro\n") and text.endswith("outro\n"))
        self.assertIn("\nmiddle\n", text)
        board = json.loads(self.board.read_text(encoding="utf-8"))
        self.assertEqual(board["test_roms"]["passing"], 14)
        self.assertEqual(board["test_roms"]["groups"][0]["group"], "timer")

    def test_check_fails_when_the_group_table_is_edited(self):
        self.write(sst_results(499), rom_results(FULL))
        self.run_mode("update")
        text = self.readme.read_text(encoding="utf-8").replace("3 / 13", "13 / 13")
        self.readme.write_text(text, encoding="utf-8")
        self.assertEqual(self.run_mode("check"), 1)

    def test_check_fails_when_the_rom_score_changes(self):
        self.write(sst_results(499), rom_results(FULL))
        self.run_mode("update")
        self.write(sst_results(499), rom_results({**FULL, "timer": (4, 13)}))
        self.assertEqual(self.run_mode("check"), 1)

    def test_partial_or_short_rom_results_are_refused(self):
        self.write(sst_results(499), rom_results(FULL, partial=True))
        with self.assertRaises(SystemExit):
            self.run_mode("update")
        self.write(sst_results(499), rom_results({"timer": (3, 13)}))
        with self.assertRaises(SystemExit):
            self.run_mode("update")

    def test_inconsistent_results_are_refused(self):
        data = sst_results(10)
        data["passing_files"] = 11
        self.write(data, rom_results(FULL))
        with self.assertRaises(SystemExit):
            self.run_mode("update")
        rom = rom_results(FULL)
        rom["passing"] += 1
        self.write(sst_results(499), rom)
        with self.assertRaises(SystemExit):
            self.run_mode("update")


if __name__ == "__main__":
    unittest.main()
