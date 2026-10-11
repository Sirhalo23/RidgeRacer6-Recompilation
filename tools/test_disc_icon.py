"""Binary-format regressions using synthetic pixels, with no game artwork."""
import struct
import unittest
import zlib

from disc_icon import PNG, decode_rgba, encode_ico, encode_png, resize_rgba
from windows_icon import icon_resources


def fixture(method, rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    raw = b"".join(bytes([method]) + bytes(row) for row in rows)
    return PNG + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


class DiscIconTests(unittest.TestCase):
    def test_all_png_filters_against_known_pixels(self):
        expected = bytes([10, 20, 30, 255, 40, 50, 60, 128,
                          70, 80, 90, 255, 100, 110, 120, 64])
        # Hand-calculated filtered bytes; the decoder must recover the same image.
        rows = [
            ([10,20,30,255,40,50,60,128], [70,80,90,255,100,110,120,64]),
            ([10,20,30,255,30,30,30,129], [70,80,90,255,30,30,30,65]),
            ([10,20,30,255,40,50,60,128], [60,60,60,0,60,60,60,192]),
            ([10,20,30,255,35,40,45,1], [65,70,75,128,45,45,45,129]),
            ([10,20,30,255,30,30,30,129], [60,60,60,0,30,30,30,192]),
        ]
        for method, scanlines in enumerate(rows):
            with self.subTest(method=method):
                self.assertEqual(decode_rgba(fixture(method, scanlines)), (2, 2, expected))

    def test_corrupt_or_truncated_png_is_rejected(self):
        data = bytearray(encode_png(1, bytes([10, 20, 30, 255])))
        data[20] ^= 1
        with self.assertRaises(ValueError):
            decode_rgba(data)
        with self.assertRaises(ValueError):
            decode_rgba(encode_png(1, bytes([10, 20, 30, 255]))[:-1])

    def test_transparent_pixels_do_not_bleed_into_resize(self):
        self.assertEqual(resize_rgba(2, 1, bytes([100, 0, 0, 255, 0, 0, 200, 0]), 1), bytes([100, 0, 0, 128]))

    def test_windows_sizes_and_resource_directory(self):
        ico = encode_ico(1, 1, bytes([10, 20, 30, 255]))
        group, images = icon_resources(ico)
        self.assertEqual(struct.unpack_from("<HHH", group), (0, 1, 7))
        self.assertEqual(len(group), 6 + 14 * 7)
        for i, size in enumerate((16, 24, 32, 48, 64, 128, 256)):
            resource_id, bitmap = images[i]
            self.assertEqual(resource_id, 400 + i)
            self.assertEqual(struct.unpack_from("<IiiHH", bitmap), (40, size, size * 2, 1, 32))
            self.assertEqual(bitmap[40:44], bytes([30, 20, 10, 255]))
            entry = struct.unpack_from("<BBBBHHIH", group, 6 + i * 14)
            self.assertEqual((entry[0] or 256, entry[1] or 256, entry[-2], entry[-1]), (size, size, len(bitmap), resource_id))
        broken = bytearray(ico)
        struct.pack_into("<I", broken, 18, len(ico) + 1)
        with self.assertRaises(ValueError):
            icon_resources(broken)

    def test_truncated_ico_is_rejected(self):
        with self.assertRaises(ValueError):
            icon_resources(b"\0\0")


if __name__ == "__main__":
    unittest.main()
