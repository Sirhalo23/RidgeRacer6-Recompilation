#!/usr/bin/env python3
"""Makes a stand-in Xbox 360 content package for testing the DLC installer.

The package holds nothing from any game: a few small files with made-up
contents, wrapped in the container format (STFS) that downloaded content uses
on the console. It is not signed and its hash tables are not real, so a
console would reject it; the SDK's reader only follows the block chains.

    python3 tools/make_test_content.py OUT_FILE [--title 4E4D07D3] [--type 2]
                                       [--name "Stand-in content"] [--magic LIVE]

--type 1 gives a "saved game" and another --title a package "for another
game"; both are for checking that the installer refuses them.

--break KIND writes a package that is damaged or doctored in one way, for
checking that the installer refuses it before writing anything:
  dotdot        a folder named ".." holding escape.txt
  backslash     a file named "..\\..\\escape.txt"
  device        a file named "CON.txt"
  same-name     two files whose names differ only in letter case
  parent-zero   the first entry claims to be inside entry 0 (itself)
  parent-range  an entry inside a folder that does not exist
  parent-file   an entry inside a file
  chain         a file whose blocks run past the end of the package
  short         the package file cut off in the middle of a file

Layout written (the read-only form used by LIVE and PIRS packages):
  0x0000  header (0x971A bytes), padded to 0xA000
  0xA000  hash table for data blocks 0..169 (one 0x18-byte entry per block:
          20 bytes of hash, then state in the top two bits and the number of
          the next block of the same file in the low 24)
  0xB000  data block 0 = the file table (0x40 bytes per entry), then the files
"""
import argparse
import struct

BLOCK = 0x1000
HEADER_SIZE = 0x971A
END_OF_CHAIN = 0xFFFFFF


def int24_le(value):
    return struct.pack("<I", value)[:3]


def build(title_id, content_type, display_name, magic, files, total_blocks_override=None):
    # Lay the files out after the file table (block 0).
    entries = []  # (name, is_dir, parent, start_block, block_count, data)
    next_block = 1
    index_of = {}
    for path, data in files:
        parts = path.split("/")
        parent = 0xFFFF
        for depth, part in enumerate(parts[:-1]):
            key = "/".join(parts[: depth + 1])
            if key not in index_of:
                index_of[key] = len(entries)
                entries.append((part, True, parent, 0, 0, b""))
            parent = index_of[key]
        blocks = max(1, -(-len(data) // BLOCK)) if data else 0
        entries.append((parts[-1], False, parent, next_block if blocks else 0, blocks, data))
        next_block += blocks
    total_blocks = next_block
    assert total_blocks <= 170, "the stand-in package uses a single hash table"
    assert len(entries) <= BLOCK // 0x40

    # Block chains: every file's blocks follow one another.
    next_of = [END_OF_CHAIN] * total_blocks
    for name, is_dir, parent, start, count, data in entries:
        for i in range(count - 1):
            next_of[start + i] = start + i + 1

    hash_table = bytearray(BLOCK)
    for block in range(total_blocks):
        info = (2 << 30) | next_of[block]  # state "allocated, in use"
        struct.pack_into(">I", hash_table, block * 0x18 + 0x14, info)
    struct.pack_into(">I", hash_table, 0xFF0, total_blocks)

    file_table = bytearray(BLOCK)
    for i, (name, is_dir, parent, start, count, data) in enumerate(entries):
        raw = name.encode("ascii")
        assert len(raw) <= 40
        off = i * 0x40
        file_table[off : off + len(raw)] = raw
        file_table[off + 40] = len(raw) | (0x80 if is_dir else 0) | (0 if is_dir else 0x40)
        file_table[off + 41 : off + 44] = int24_le(count)
        file_table[off + 44 : off + 47] = int24_le(count)
        file_table[off + 47 : off + 50] = int24_le(start)
        struct.pack_into(">H", file_table, off + 50, parent)
        struct.pack_into(">I", file_table, off + 52, len(data))
        # FAT dates: 2026-10-07 12:00:00
        date = ((2026 - 1980) << 9) | (10 << 5) | 7
        time = 12 << 11
        struct.pack_into(">HHHH", file_table, off + 56, date, time, date, time)

    header = bytearray(0xA000)
    header[0:4] = magic.encode("ascii")
    # One licence with flags set, so that the installer records licence bits.
    struct.pack_into(">QII", header, 0x22C, 0xFFFFFFFFFFFFFFFF, 0x00000001, 0x00000001)
    struct.pack_into(">I", header, 0x340, HEADER_SIZE)
    struct.pack_into(">I", header, 0x344, content_type)
    struct.pack_into(">I", header, 0x348, 1)  # metadata version
    struct.pack_into(">Q", header, 0x34C, total_blocks * BLOCK)
    struct.pack_into(">I", header, 0x360, title_id)
    # Volume descriptor (STFS) at 0x379.
    vd = 0x379
    header[vd] = 0x24
    header[vd + 2] = 0x01  # read-only format: one hash table per level
    struct.pack_into("<H", header, vd + 3, 1)  # file table block count
    header[vd + 5 : vd + 8] = int24_le(0)  # file table block number
    struct.pack_into(">I", header, vd + 0x1C, total_blocks_override or total_blocks)
    struct.pack_into(">I", header, vd + 0x20, 0)
    struct.pack_into(">I", header, 0x39D, 0)  # data file count (0 = single file)
    struct.pack_into(">I", header, 0x3A9, 0)  # volume type: STFS
    name16 = display_name.encode("utf-16-be")[: 127 * 2]
    for language in range(9):
        start = 0x411 + language * 0x100
        header[start : start + len(name16)] = name16
    title16 = "Stand-in content (not from any game)".encode("utf-16-be")
    header[0x1691 : 0x1691 + len(title16)] = title16

    out = bytearray(header)
    out += hash_table
    out += file_table
    for name, is_dir, parent, start, count, data in entries:
        if count:
            out += data + b"\0" * (count * BLOCK - len(data))
    return bytes(out)


FILE_TABLE = 0xB000  # data block 0


def entry_offset(package, name):
    for i in range(64):
        off = FILE_TABLE + i * 0x40
        length = package[off + 40] & 0x3F
        if package[off : off + length] == name.encode("ascii"):
            return off
    raise KeyError(name)


def rename(package, old, new):
    off = entry_offset(package, old)
    raw = new.encode("ascii")
    package[off : off + 40] = raw + b"\0" * (40 - len(raw))
    package[off + 40] = (package[off + 40] & 0xC0) | len(raw)


def set_parent(package, name, parent):
    struct.pack_into(">H", package, entry_offset(package, name) + 50, parent)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out")
    ap.add_argument("--title", default="4E4D07D3")
    ap.add_argument("--type", type=lambda v: int(v, 0), default=2)
    ap.add_argument("--name", default="Stand-in content")
    ap.add_argument("--magic", default="LIVE", choices=["LIVE", "PIRS", "CON "])
    ap.add_argument("--break", dest="damage", choices=[
        "dotdot", "backslash", "device", "same-name", "parent-zero", "parent-range",
        "parent-file", "chain", "short"])
    args = ap.parse_args()

    big = bytes((i * 7 + (i >> 8)) & 0xFF for i in range(3 * BLOCK + 123))
    files = [
        ("readme.txt", b"A stand-in content package for testing. Nothing from any game.\n"),
        ("data/pattern.bin", big),
        ("data/sub/empty.bin", b""),
        ("data/sub/small.bin", b"\x01\x02\x03\x04"),
    ]
    if args.damage == "dotdot":
        files.append(("../escape.txt", b"This file must not be written outside the package.\n"))
    elif args.damage in ("backslash", "device"):
        files.append(("escape.txt", b"This file must not be written.\n"))
    elif args.damage == "same-name":
        files += [("same.txt", b"one\n"), ("SAME.TXT", b"two\n")]
    package = bytearray(build(int(args.title, 16), args.type, args.name, args.magic, files))
    if args.damage == "backslash":
        rename(package, "escape.txt", "..\\..\\escape.txt")
    elif args.damage == "device":
        rename(package, "escape.txt", "CON.txt")
    elif args.damage == "parent-zero":
        set_parent(package, "readme.txt", 0)
    elif args.damage == "parent-range":
        set_parent(package, "pattern.bin", 0x0100)
    elif args.damage == "parent-file":
        set_parent(package, "pattern.bin", 0)  # entry 0 is readme.txt
    elif args.damage == "chain":
        struct.pack_into(">I", package, 0xA000 + 2 * 0x18 + 0x14, (2 << 30) | 0x000500)  # pattern.bin starts at block 2
    elif args.damage == "short":
        del package[-3 * BLOCK:]
    with open(args.out, "wb") as f:
        f.write(package)
    print(f"{args.out}: {len(package)} bytes, title {args.title}, content type {args.type:#x}")


if __name__ == "__main__":
    main()
