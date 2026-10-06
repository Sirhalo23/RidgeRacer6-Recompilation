#!/usr/bin/env python3
"""List every 'lwzx-from-table; mtctr; bctr' site and say whether the table is a real
switch jump table (entries are labels inside the same function) or a table of
function pointers (an indirect tail call that must NOT be compiled as a switch).
usage: find_dispatch_tables.py <unpacked image> <generated *_init.cpp>
"""
import sys, struct, re, bisect
BASE = 0x82000000
img = open(sys.argv[1], 'rb').read()
funcs = sorted(int(m, 16) for m in re.findall(r'\{ 0x([0-9A-F]{8}), sub_', open(sys.argv[2]).read()))
fset = set(funcs)
def u32(a): return struct.unpack_from('>I', img, a - BASE)[0]
def owner(a): return funcs[bisect.bisect_right(funcs, a) - 1]
def next_func(a):
    i = bisect.bisect_right(funcs, a)
    return funcs[i] if i < len(funcs) else 0xFFFFFFFF
pe = struct.unpack_from('<I', img, 0x3c)[0]
nsec = struct.unpack_from('<H', img, pe + 6)[0]; optsz = struct.unpack_from('<H', img, pe + 20)[0]
secs = []
o = pe + 24 + optsz
for i in range(nsec):
    name = img[o:o+8].rstrip(b'\0').decode(); vs, va = struct.unpack_from('<II', img, o + 8)
    secs.append((BASE + va, BASE + va + vs, name)); o += 40
def sec(a):
    for lo, hi, n in secs:
        if lo <= a < hi: return n
    return '?'
code = [(lo, hi) for lo, hi, n in secs if n in ('.text', 'PSFD00')]
rows = []
for lo, hi in code:
    for a in range(lo, hi - 3, 4):
        if u32(a) != 0x4E800420: continue            # bctr
        ctr_src = None; load = None; regs = {}
        for k in range(1, 12):
            w = u32(a - 4 * k); op = w >> 26
            if w in (0x4E800020, 0x4E800420, 0): break
            if ctr_src is None:
                if op == 31 and (w >> 1) & 0x3FF == 467 and ((w >> 11) & 0x3FF) == 0x120:   # mtctr rS
                    ctr_src = (w >> 21) & 31
                continue
            if load is None:
                if op == 31 and (w >> 1) & 0x3FF == 23 and (w >> 21) & 31 == ctr_src:      # lwzx rD,rA,rB
                    load = ((w >> 16) & 31, (w >> 11) & 31)
                elif (w >> 21) & 31 == ctr_src and op in (32, 14, 31):                      # something else writes it
                    break
                continue
            if op == 14 and (w >> 21) & 31 in load:                                          # addi rT,rT,lo
                regs[(w >> 21) & 31] = ('lo', w & 0xFFFF, (w >> 16) & 31)
            if op == 15 and (w >> 16) & 31 == 0:                                             # lis rT,hi
                rd = (w >> 21) & 31
                for r, (_, lo16, src) in list(regs.items()):
                    if src == rd:
                        t = (((w & 0xFFFF) << 16) + lo16 - (0x10000 if lo16 & 0x8000 else 0)) & 0xFFFFFFFF
                        rows.append((a, t)); regs = {}; load = 'done'
                        break
                if load == 'done': break
        # note: tables whose lis is shared/hoisted further away are not found here
out = []
for a, t in rows:
    f = owner(a); fend = next_func(a)
    ents = []
    for i in range(64):
        if t - BASE + 4 * i + 4 > len(img): break
        v = u32(t + 4 * i)
        if not (0x820C0000 <= v < 0x82340000) or v & 3: break
        ents.append(v)
    inside = sum(1 for v in ents if f <= v < fend)
    starts = sum(1 for v in ents if v in fset)
    kind = 'switch' if ents and inside == len(ents) and sec(t) in ('.text', 'PSFD00', '.rdata') and starts <= 1 else \
           ('FUNCPTR' if ents and starts == len(ents) else 'mixed')
    out.append((a, f, t, sec(t), len(ents), inside, starts, kind))
from collections import Counter
print('# sites:', len(out), dict(Counter(k for *_, k in out)))
for a, f, t, s, n, inside, starts, kind in out:
    if kind != 'switch':
        print(f'bctr {a:08X} in sub_{f:08X} table {t:08X} ({s}) entries {n} inside_func {inside} func_starts {starts} -> {kind}')
