#!/usr/bin/env python3
"""Extract local disc artwork and make Windows/Linux icons using only Python's stdlib."""
import argparse
from hashlib import sha256
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

SUPPORTED_XEX = "39d3c0004ec62aeb0fe3e7e1889cf25d98fbd27987b6bc6b5ff30a56ffba6c00"
PNG = b"\x89PNG\r\n\x1a\n"


def extract_title_icon(xex, rexglue, directory):
    xex, rexglue = Path(xex).resolve(), Path(rexglue).resolve()
    if sha256(xex.read_bytes()).hexdigest() != SUPPORTED_XEX:
        raise ValueError("Icon source must be the supported USA disc's default.xex")
    if not rexglue.is_file():
        raise ValueError(f"Build or install the SDK's rexglue tool first: {rexglue}")
    # Older SDK CLIs require init's parent options even for this subcommand.
    subprocess.run([str(rexglue), "init", "--project-name", "rr6-icon", "--xex-path",
                    str(xex), "--project-root", str(directory), "achievements",
                    str(xex), str(directory)], check=True)
    artwork = Path(directory) / "icons/title.png"
    data = artwork.read_bytes()
    if not data.startswith(PNG):
        raise ValueError("No PNG title icon was extracted from the disc executable")
    return artwork


def decode_rgba(data):
    """Decode the supported disc's 8-bit, noninterlaced RGBA PNG, including all filters."""
    if not data.startswith(PNG):
        raise ValueError("Not a PNG")
    offset, compressed, dimensions, ended = 8, bytearray(), None, False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError("Truncated PNG chunk")
        length, kind = struct.unpack_from(">I4s", data, offset)
        end = offset + 12 + length
        if end > len(data):
            raise ValueError("Truncated PNG payload")
        payload = data[offset + 8:end - 4]
        if zlib.crc32(kind + payload) != struct.unpack_from(">I", data, end - 4)[0]:
            raise ValueError("PNG checksum mismatch")
        if kind == b"IHDR":
            if dimensions is not None or length != 13:
                raise ValueError("Invalid PNG header")
            width, height, bits, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            if not (0 < width <= 256 and 0 < height <= 256) or (bits, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
                raise ValueError("Expected a small 8-bit RGBA title PNG")
            dimensions = width, height
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            ended = True
            break
        offset = end
    if dimensions is None or not ended:
        raise ValueError("Incomplete PNG")
    width, height = dimensions
    stride = width * 4
    size = height * (stride + 1)
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed, size + 1)
    if len(raw) != size or not decoder.eof or decoder.unused_data:
        raise ValueError("Invalid PNG decompressed size")
    pixels, previous = bytearray(), bytearray(stride)
    for row in range(height):
        start = row * (stride + 1)
        method = raw[start]
        scan = bytearray(raw[start + 1:start + 1 + stride])
        if method > 4:
            raise ValueError("Unknown PNG filter")
        for i in range(stride):
            left = scan[i - 4] if i >= 4 else 0
            above = previous[i]
            diagonal = previous[i - 4] if i >= 4 else 0
            if method == 1:
                prediction = left
            elif method == 2:
                prediction = above
            elif method == 3:
                prediction = (left + above) // 2
            elif method == 4:
                p = left + above - diagonal
                distances = abs(p - left), abs(p - above), abs(p - diagonal)
                prediction = (left, above, diagonal)[distances.index(min(distances))]
            else:
                prediction = 0
            scan[i] = (scan[i] + prediction) & 255
        pixels.extend(scan)
        previous = scan
    return width, height, bytes(pixels)


def resize_rgba(width, height, pixels, size):
    # Premultiply during interpolation so transparent pixels do not create halos.
    result = bytearray()
    for y in range(size):
        sy = max(0, min(height - 1, (y + .5) * height / size - .5))
        y0, fy = int(sy), sy - int(sy)
        for x in range(size):
            sx = max(0, min(width - 1, (x + .5) * width / size - .5))
            x0, fx = int(sx), sx - int(sx)
            samples = []
            for xx, yy, weight in ((x0, y0, (1-fx)*(1-fy)), (min(x0+1, width-1), y0, fx*(1-fy)),
                                   (x0, min(y0+1, height-1), (1-fx)*fy), (min(x0+1, width-1), min(y0+1, height-1), fx*fy)):
                i = (yy * width + xx) * 4
                samples.append((pixels[i:i+4], weight))
            alpha = sum(p[3] * weight for p, weight in samples)
            result.extend(round(sum(p[c] * p[3] * weight for p, weight in samples) / alpha) if alpha else 0 for c in range(3))
            result.append(round(alpha))
    return bytes(result)


def encode_png(size, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    scanlines = b"".join(b"\0" + pixels[y*size*4:(y+1)*size*4] for y in range(size))
    return PNG + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b"")


def encode_ico(width, height, pixels):
    sizes = (16, 24, 32, 48, 64, 128, 256)
    entries, images = [], []
    offset = 6 + 16 * len(sizes)
    for size in sizes:
        rgba = resize_rgba(width, height, pixels, size)
        # Traditional 32-bit DIBs work for all Windows icon sizes, including 256.
        bgra = bytearray()
        mask_stride = ((size + 31) // 32) * 4
        mask = bytearray(mask_stride * size)
        for y in range(size - 1, -1, -1):
            for x in range(size):
                i = (y * size + x) * 4
                r, g, b, a = rgba[i:i+4]
                bgra.extend((b, g, r, a))
                if a == 0:
                    mask[(size - 1 - y) * mask_stride + x // 8] |= 128 >> (x % 8)
        image = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(bgra), 0, 0, 0, 0) + bgra + mask
        entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(image), offset))
        images.append(image)
        offset += len(image)
    return struct.pack("<HHH", 0, 1, len(sizes)) + b"".join(entries) + b"".join(images)


def make_icons(xex, rexglue, output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rr6-disc-icon-") as directory:
        artwork = extract_title_icon(xex, rexglue, directory)
        width, height, pixels = decode_rgba(artwork.read_bytes())
        (output / "rr6.ico").write_bytes(encode_ico(width, height, pixels))
        (output / "rr6.png").write_bytes(encode_png(256, resize_rgba(width, height, pixels, 256)))
    return output / "rr6.ico"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xex", type=Path)
    parser.add_argument("rexglue", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--embed", type=Path, action="append", default=[], help="Windows only: replace icon in a staged EXE")
    args = parser.parse_args()
    ico = make_icons(args.xex, args.rexglue, args.output)
    if args.embed:
        from windows_icon import embed_icon
        for executable in args.embed:
            embed_icon(executable, ico)
    print(f"Created local ISO-derived Windows/Linux icons in {args.output}")
