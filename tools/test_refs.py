import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "roms"))
import fetch_roms  # noqa: E402


def png(width, height, depth, colour, rows):
    def chunk(kind, payload):
        return (len(payload).to_bytes(4, "big") + kind + payload
                + zlib.crc32(kind + payload).to_bytes(4, "big"))

    header = width.to_bytes(4, "big") + height.to_bytes(4, "big") + bytes([depth, colour, 0, 0, 0])
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


class RefsTest(unittest.TestCase):
    def test_truecolour_shades(self):
        row = []
        for x in range(160):
            row += [(255, 255, 255), (170, 170, 170), (85, 85, 85), (0, 0, 0)][x % 4]
        shades = fetch_roms.decode_png(png(160, 144, 8, 2, [row] * 144))
        self.assertEqual(len(shades), 160 * 144)
        self.assertEqual(shades[0:4], [0, 1, 2, 3])

    def test_two_bit_greyscale_is_inverted(self):
        row = bytes([0b11100100] * 40)  # values 3,2,1,0 repeating -> shades 0,1,2,3
        shades = fetch_roms.decode_png(png(160, 144, 2, 0, [row] * 144))
        self.assertEqual(shades[0:4], [0, 1, 2, 3])

    def test_unsupported_format_is_refused(self):
        with self.assertRaises(SystemExit):
            fetch_roms.decode_png(png(160, 144, 8, 6, [[0] * 640] * 144))
        with self.assertRaises(SystemExit):
            fetch_roms.decode_png(png(80, 72, 8, 2, [[0] * 240] * 72))


if __name__ == "__main__":
    unittest.main()
