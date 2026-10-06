#!/usr/bin/env python3
"""Find code addresses that the image references as pointers (via .reloc) but
that the recompiler did not register as function entry points.
These are indirect-call targets (static constructors, vtable slots, callbacks)
that would otherwise die at runtime with "Call to invalid or unregistered function".
usage: find_orphan_targets.py <unpacked image> <generated *_init.cpp> [generated dir for labels]
"""
import sys, struct, re, glob, os
BASE = 0x82000000
img = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', img, 0x3c)[0]
nsec = struct.unpack_from('<H', img, pe + 6)[0]
optsz = struct.unpack_from('<H', img, pe + 20)[0]
secs = {}
o = pe + 24 + optsz
for i in range(nsec):
    name = img[o:o + 8].rstrip(b'\0').decode()
    vs, va = struct.unpack_from('<II', img, o + 8)
    fl = struct.unpack_from('<I', img, o + 36)[0]
    secs[name] = (BASE + va, vs, fl)
    o += 40
code = [(a, a + s) for n, (a, s, f) in secs.items() if f & 0x20]
def in_code(a): return any(lo <= a < hi for lo, hi in code)
def u32(a): return struct.unpack_from('>I', img, a - BASE)[0]

funcs = set(int(m, 16) for m in re.findall(r'\{ 0x([0-9A-F]{8}), ', open(sys.argv[2]).read()))
labels = set()
if len(sys.argv) > 3:
    for f in glob.glob(os.path.join(sys.argv[3], '*_recomp.*.cpp')):
        labels |= set(int(m, 16) for m in re.findall(r'loc_([0-9A-F]{8}):', open(f).read()))

# The XEX build strips .reloc (the title resource overlays it), so find
# pointers by scanning instead.
data_ptrs, code_ptrs = {}, {}
# (a) aligned big-endian words in data sections that point into code
for n, (sa, ss, fl) in secs.items():
    if fl & 0x20 or n in ('.reloc', '.XBLD', '.idata', '.XEXID', '.pdata'):
        continue
    for addr in range(sa + (-sa % 4), min(sa + ss, BASE + len(img)) - 3, 4):
        v = u32(addr)
        if 0x82000000 <= v < 0x83000000:
            data_ptrs.setdefault(v, []).append(addr)
# (b) lis rX,hi ... addi/ori rY,rX,lo within the next few instructions
for lo_, hi_ in code:
    for addr in range(lo_, hi_ - 3, 4):
        w = u32(addr)
        if w >> 26 == 15 and (w >> 16) & 31 == 0:          # lis rD, imm
            rd = (w >> 21) & 31; hi = w & 0xFFFF
            if hi not in (0x820C + i for i in range(0x28)):
                continue
            for k in range(1, 6):
                if addr + 4 * k >= hi_: break
                x = u32(addr + 4 * k)
                op = x >> 26
                if op == 14 and (x >> 16) & 31 == rd:       # addi rT, rD, lo
                    lo = x & 0xFFFF
                    v = ((hi << 16) + lo - (0x10000 if lo & 0x8000 else 0)) & 0xFFFFFFFF
                    code_ptrs.setdefault(v, []).append(addr)
                elif op == 24 and (x >> 21) & 31 == rd:     # ori rT, rD, lo
                    code_ptrs.setdefault((hi << 16) | (x & 0xFFFF), []).append(addr)
                # stop if rD is overwritten by another lis
                if op == 15 and (x >> 21) & 31 == rd: break

# .pdata: one record per non-leaf function (begin, packed length)
pa, ps, _ = secs['.pdata']
pdata = []
for o in range(pa - BASE, pa - BASE + ps - 7, 8):
    b, w = struct.unpack_from('>II', img, o)
    if b == 0: continue
    pdata.append((b, b + ((w >> 8) & 0x3FFFFF) * 4))
pdata.sort()
import bisect
pstarts = [b for b, e in pdata]
def inside_pdata_func(a):
    i = bisect.bisect_right(pstarts, a) - 1
    return i >= 0 and pdata[i][0] < a < pdata[i][1]

def prev_is_terminator(a):
    w = u32(a - 4)
    return w == 0 or w == 0x4E800020 or w == 0x4E800420 or (w >> 26 == 18 and not w & 1)
cands = {}
for src, tag in ((data_ptrs, 'data'), (code_ptrs, 'code')):
    for v, refs in src.items():
        if v % 4 == 0 and in_code(v) and v not in funcs:
            cands.setdefault(v, []).append((tag, len(refs), refs[0]))
rows = []
for v in sorted(cands):
    first = u32(v)
    if v in labels:                       kind = 'label'        # jump-table base / known mid-function label
    elif in_code(first) or first == 0:    kind = 'table'        # address table or padding embedded in .text
    elif inside_pdata_func(v):            kind = 'midfunc'      # SEH/EH scope target inside a function
    elif prev_is_terminator(v):           kind = 'FUNCTION'     # starts right after a terminator, no pdata owner
    else:                                 kind = 'unclear'
    rows.append((v, kind, ','.join(f'{t}x{n}@{r:08X}' for t, n, r in cands[v])))
from collections import Counter
print(f'# functions registered: {len(funcs)}; code labels: {len(labels)}; pdata records: {len(pdata)}')
print('# classes:', dict(Counter(k for _, k, _ in rows)))
for v, k, refs in rows:
    print(f'{v:08X} {k:8} {refs}')
