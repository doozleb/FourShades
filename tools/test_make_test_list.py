import os
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent / "roms"))
import make_test_list  # noqa: E402


def entry(name, method="mooneye", references=("x.png",)):
    refs = list(references)
    return {"name": name, "rom": name, "group": "screen", "method": method,
            "informational": make_test_list.informational_for(method, refs),
            "runtime": 1.0, "limit_seconds": 6.0, "references": refs}


def full_list(extra_informational=()):
    """167 entries: the two known informational tests plus 165 scored ones."""
    entries = [entry(name, "screenshot", ()) for name in make_test_list.INFORMATIONAL]
    entries += [entry(name, "screenshot", ()) for name in extra_informational]
    entries += [entry(f"t{i}.gb") for i in range(make_test_list.EXPECTED - len(entries))]
    return entries


class RequestTest(unittest.TestCase):
    def test_the_tree_request_carries_the_github_token_when_set(self):
        with mock.patch.dict(os.environ, {"GITHUB_TOKEN": "t0ken"}):
            request = make_test_list.request_for(make_test_list.TREE)
        self.assertEqual(request.get_header("Authorization"), "Bearer t0ken")
        self.assertEqual(request.full_url, make_test_list.TREE)

    def test_no_token_no_header(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertIsNone(make_test_list.request_for(make_test_list.TREE).get_header("Authorization"))

    def test_the_token_is_never_sent_to_raw_githubusercontent(self):
        with mock.patch.dict(os.environ, {"GITHUB_TOKEN": "t0ken"}):
            request = make_test_list.request_for(make_test_list.RAW + "testroms/blargg.py")
        self.assertIsNone(request.get_header("Authorization"))


class InformationalTest(unittest.TestCase):
    def test_informational_only_for_a_screenshot_test_with_no_reference(self):
        self.assertTrue(make_test_list.informational_for("screenshot", []))
        self.assertFalse(make_test_list.informational_for("screenshot", ["a.png"]))
        self.assertFalse(make_test_list.informational_for("mooneye", []))
        self.assertFalse(make_test_list.informational_for("blargg", []))

    def test_the_shootout_list_scores_165_of_167(self):
        self.assertEqual(make_test_list.EXPECTED, 167)
        self.assertEqual(make_test_list.EXPECTED_SCORED, 165)
        self.assertEqual(set(make_test_list.INFORMATIONAL), {"acid/which.gb (DMG)", "daid/rom_and_ram.gb"})
        make_test_list.check_counts(full_list())  # no SystemExit

    def test_an_unexpected_screenshot_test_with_no_reference_fails_loudly(self):
        entries = full_list(extra_informational=["daid/new.gb"])
        with self.assertRaises(SystemExit):
            make_test_list.check_counts(entries)

    def test_a_missing_informational_test_fails_loudly(self):
        entries = full_list()
        entries[0] = entry("replacement.gb")
        with self.assertRaises(SystemExit):
            make_test_list.check_counts(entries)

    def test_the_wrong_total_fails_loudly(self):
        with self.assertRaises(SystemExit):
            make_test_list.check_counts(full_list()[:-1])


if __name__ == "__main__":
    unittest.main()
