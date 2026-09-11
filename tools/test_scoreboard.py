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
