import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "roms"))
import fetch_roms  # noqa: E402


def png(width, height, depth, colour, rows, palette=None):
    def chunk(kind, payload):
        return (len(payload).to_bytes(4, "big") + kind + payload
                + zlib.crc32(kind + payload).to_bytes(4, "big"))

    header = width.to_bytes(4, "big") + height.to_bytes(4, "big") + bytes([depth, colour, 0, 0, 0])
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    body = chunk(b"IHDR", header)
    if palette is not None:
        body += chunk(b"PLTE", b"".join(bytes(rgb) for rgb in palette))
    body += chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    return b"\x89PNG\r\n\x1a\n" + body


def pack_samples(values, depth):
    """Packs `values` (each < 2**depth) into big-endian, MSB-first bytes,
    per the PNG spec's bit order -- the inverse of fetch_roms.read_samples."""
    per_byte = 8 // depth
    out = bytearray()
    for start in range(0, len(values), per_byte):
        group = values[start:start + per_byte]
        byte = 0
        for j, value in enumerate(group):
            shift = 8 - depth * (j + 1)
            byte |= (value & ((1 << depth) - 1)) << shift
        out.append(byte)
    return bytes(out)


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

    def test_one_bit_greyscale_shades(self):
        # Samples alternate 1,0,1,0,... . maximum = 2**1 - 1 = 1, so the PNG
        # spec's linear scaling (sample * 255 // maximum) sends 1 -> 255 and
        # 0 -> 0. SHADE_FOR_GREY maps grey 255 -> shade 0 and grey 0 -> shade 3.
        #
        # Hand-encoded, not built via pack_samples: the PNG spec packs samples
        # MSB-first, 8 per byte at depth 1, so the leftmost sample of each
        # group of 8 sits in bit 7 and the rightmost in bit 0. The repeating
        # sample pattern 1,0,1,0,1,0,1,0 packs into one byte as:
        #   bit:    7 6 5 4 3 2 1 0
        #   sample: 1 0 1 0 1 0 1 0
        #   byte  = 0b10101010 = 0xAA
        # and every byte in the row is that same 0xAA since the 2-sample
        # pattern divides evenly into the 8-sample byte.
        row = bytes([0b10101010] * 20)  # 160 samples / 8 per byte = 20 bytes
        shades = fetch_roms.decode_png(png(160, 144, 1, 0, [row] * 144))
        self.assertEqual(len(shades), 160 * 144)
        self.assertEqual(shades[0:4], [0, 3, 0, 3])

    def test_eight_bit_greyscale_shades(self):
        # At 8-bit depth, maximum = 255, so scaling is the identity: the raw
        # sample value is the grey level. SHADE_FOR_GREY: 255->0, 170->1, 85->2, 0->3.
        samples = [255, 170, 85, 0] * 40
        row = pack_samples(samples, 8)
        shades = fetch_roms.decode_png(png(160, 144, 8, 0, [row] * 144))
        self.assertEqual(len(shades), 160 * 144)
        self.assertEqual(shades[0:4], [0, 1, 2, 3])

    def test_four_bit_indexed_shades(self):
        # PLTE entry i is looked up directly (no scaling) and then run through
        # SHADE_FOR_RGB: (0,0,0)->3, (85,85,85)->2, (170,170,170)->1, (255,255,255)->0.
        #
        # Hand-encoded, not built via pack_samples: the PNG spec packs samples
        # MSB-first, 2 per byte at depth 4, so the first index of each pair
        # sits in the high nibble and the second in the low nibble. The
        # repeating index pattern 0,1,2,3 packs into bytes as:
        #   pair (0,1) -> high nibble 0, low nibble 1 -> 0x01
        #   pair (2,3) -> high nibble 2, low nibble 3 -> 0x23
        # and the pattern repeats every 2 bytes since the 4-index pattern
        # divides evenly into the 2-index byte.
        palette = [(0, 0, 0), (85, 85, 85), (170, 170, 170), (255, 255, 255)]
        row = bytes([0x01, 0x23] * 40)  # 160 indices / 2 per byte = 80 bytes
        shades = fetch_roms.decode_png(png(160, 144, 4, 3, [row] * 144, palette=palette))
        self.assertEqual(len(shades), 160 * 144)
        self.assertEqual(shades[0:4], [3, 2, 1, 0])

    def test_green_dmg_palette_matches_grey_palette(self):
        # SHADE_FOR_RGB maps the green-palette lightest/darkest tuples to the
        # same shades as their grey counterparts: (224,248,208)->0 like
        # (255,255,255)->0, and (8,24,32)->3 like (0,0,0)->3. The middle two
        # shades are grey in both palettes. So a green-palette reference must
        # decode to exactly the same shade sequence as an all-grey one built
        # from the same x%4 pattern -- [0, 1, 2, 3] repeating -- derived here
        # from the SHADE_FOR_RGB table, not by decoding a grey PNG and diffing.
        green_row = []
        for x in range(160):
            green_row += [(224, 248, 208), (170, 170, 170), (85, 85, 85), (8, 24, 32)][x % 4]
        shades = fetch_roms.decode_png(png(160, 144, 8, 2, [green_row] * 144))
        expected = [0, 1, 2, 3] * (160 * 144 // 4)
        self.assertEqual(shades, expected)

    def test_truecolour_pixel_not_a_shade_aborts(self):
        row = [1, 2, 3] * 160
        with self.assertRaises(SystemExit):
            fetch_roms.decode_png(png(160, 144, 8, 2, [row] * 144))

    def test_grey_level_not_a_shade_aborts(self):
        samples = [128] * 160  # not one of 0, 85, 170, 255
        row = pack_samples(samples, 8)
        with self.assertRaises(SystemExit):
            fetch_roms.decode_png(png(160, 144, 8, 0, [row] * 144))

    def test_palette_index_out_of_range_aborts(self):
        palette = [(255, 255, 255), (0, 0, 0)]  # only indices 0, 1 are valid
        indices = [0, 1, 2, 3] * 40  # 2 and 3 are out of range
        row = pack_samples(indices, 4)
        with self.assertRaises(SystemExit):
            fetch_roms.decode_png(png(160, 144, 4, 3, [row] * 144, palette=palette))

    def test_indexed_png_without_palette_aborts(self):
        row = pack_samples([0] * 160, 4)
        with self.assertRaises(SystemExit):
            fetch_roms.decode_png(png(160, 144, 4, 3, [row] * 144))  # no palette= given


if __name__ == "__main__":
    unittest.main()
