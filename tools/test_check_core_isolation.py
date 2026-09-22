import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_core_isolation  # noqa: E402


class RomNameTest(unittest.TestCase):
    """The word list cannot see a ROM's name, which is how eight of them got
    into src/core comments. These are the names that were there, plus the
    spellings the suites use around them."""

    def matched(self, text):
        found = check_core_isolation.ROM_NAME.search(text)
        return found.group(0) if found else None

    def test_catches_the_names_that_got_in(self):
        for name in ("m3_bgp_change", "m3_wx_4_change", "m3_wx_5_change",
                     "m3_window_timing", "intr_2_mode0_timing_sprites",
                     "intr_1_2_timing-GS", "lcdon_timing-GS", "stat_lyc_onoff",
                     "line_0_fix"):
            with self.subTest(name=name):
                self.assertIsNotNone(self.matched(f"// pinned by {name}"))

    def test_catches_other_suite_spellings(self):
        for name in ("boot_div-dmgABCmgb", "oam_dma_restart", "6-timing_no_bug",
                     "dmg-acid2.gb", "m3_lcdc_win_en_change_multiple"):
            with self.subTest(name=name):
                self.assertIsNotNone(self.matched(f"// see {name} for this"))

    def test_leaves_the_core_alone(self):
        for text in ("    int dotsRemaining(const Ppu& ppu) const;",
                     "    windowReached_ = true;",
                     "    std::array<u8, kWidth * kHeight> frame_{};",
                     "// Pan Docs: in Non-CGB mode, the smaller the X coordinate",
                     "    for (std::size_t i = 0; i + 1 < objects_.size(); ++i) {",
                     "// verified on DMG, MGB, SGB, SGB2, CGB, AGB and AGS hardware"):
            with self.subTest(text=text):
                self.assertIsNone(self.matched(text))

    def test_the_core_passes_the_check_as_committed(self):
        self.assertEqual(check_core_isolation.main(), 0)


if __name__ == "__main__":
    unittest.main()
