#!/usr/bin/env python3
"""Minimal resumable XDVDFS (Xbox 360 game partition) lister/extractor.
usage: extract_xiso.py <iso> list
       extract_xiso.py <iso> extract <outdir> [time_budget_seconds]
"""
import sys, os, struct, time
SECTOR = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'
OFFSETS = (0xFD90000, 0x2080000, 0x18300000, 0)

def find_partition(f):
    for off in OFFSETS:
        f.seek(off + 0x10000)
        d = f.read(28)
        if d[:20] == MAGIC:
            rs, rsz = struct.unpack('<II', d[20:28])
            return off, rs, rsz
    raise SystemExit('no XDVDFS partition found')

def walk(f, base, sector, size, path, out):
    if size == 0:
        return
    f.seek(base + sector * SECTOR)
    buf = f.read(size)
    stack = [0]
    seen = set()
    while stack:
        o = stack.pop()
        if o in seen or o + 14 > len(buf):
            continue
        seen.add(o)
        l, r, sec, sz, attr, nlen = struct.unpack_from('<HHIIBB', buf, o)
        if l == 0xFFFF and r == 0xFFFF and sec == 0xFFFFFFFF:
            continue
        name = buf[o + 14:o + 14 + nlen].decode('latin-1')
        p = path + [name]
        if attr & 0x10:
            out.append(('d', '/'.join(p), sec, sz))
            walk(f, base, sec, sz, p, out)
            f.seek(0)
        else:
            out.append(('f', '/'.join(p), sec, sz))
        if l and l != 0xFFFF:
            stack.append(l * 4)
        if r and r != 0xFFFF:
            stack.append(r * 4)

def main():
    iso, mode = sys.argv[1], sys.argv[2]
    f = open(iso, 'rb')
    base, rs, rsz = find_partition(f)
    entries = []
    walk(f, base, rs, rsz, [], entries)
    files = [e for e in entries if e[0] == 'f']
    if mode == 'list':
        for k, p, sec, sz in sorted(entries, key=lambda e: e[1].lower()):
            print(f'{k} {sz:>12} {p}')
        print(f'# {len(files)} files, {sum(e[3] for e in files)} bytes, '
              f'{len(entries) - len(files)} dirs', file=sys.stderr)
        return
    out = sys.argv[3]
    budget = float(sys.argv[4]) if len(sys.argv) > 4 else 1e18
    t0 = time.time()
    for k, p, sec, sz in entries:
        if k == 'd':
            os.makedirs(os.path.join(out, p), exist_ok=True)
    done = 0
    for k, p, sec, sz in sorted(files, key=lambda e: e[2]):
        dst = os.path.join(out, p)
        os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
        if os.path.exists(dst) and os.path.getsize(dst) == sz:
            done += 1
            continue
        if time.time() - t0 > budget:
            print(f'PARTIAL {done}/{len(files)}')
            return
        tmp = dst + '.part'
        have = os.path.getsize(tmp) if os.path.exists(tmp) else 0
        have -= have % (8 << 20)          # resume on a chunk boundary
        f.seek(base + sec * SECTOR + have)
        left = sz - have
        with open(tmp, 'r+b' if have else 'wb') as o:
            o.seek(have)
            o.truncate()
            while left:
                if time.time() - t0 > budget:
                    print(f'PARTIAL {done}/{len(files)} (mid-file {p})')
                    return
                chunk = f.read(min(left, 8 << 20))
                if not chunk:
                    raise SystemExit(f'short read in {p}')
                o.write(chunk)
                left -= len(chunk)
        os.replace(tmp, dst)
        done += 1
    print(f'COMPLETE {done}/{len(files)}')

main()
