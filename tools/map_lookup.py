#!/usr/bin/env python3
"""Resolve crash offsets (WER 'P8' values) against the linker map.
usage: map_lookup.py <rr6_recomp.map> <hex offset> [...]"""
import sys, re, bisect
syms = []; base = None
for l in open(sys.argv[1], errors='replace'):
    m = re.match(r'\s*Preferred load address is ([0-9a-fA-F]+)', l)
    if m: base = int(m.group(1), 16)
    m = re.match(r'\s*([0-9a-f]{4}):([0-9a-f]{8})\s+(\S+)\s+([0-9a-f]{16})', l)
    if m: syms.append((int(m.group(4), 16), m.group(3)))
syms.sort(); addrs = [s[0] for s in syms]
for a in sys.argv[2:]:
    v = base + int(a, 16)
    i = bisect.bisect_right(addrs, v) - 1
    nxt = syms[i + 1][0] if i + 1 < len(syms) else v
    names = sorted({n for ad, n in syms if ad == syms[i][0]})
    print(f'{a}: {" / ".join(names)} +{v - syms[i][0]:#x} (function size {nxt - syms[i][0]:#x})')
