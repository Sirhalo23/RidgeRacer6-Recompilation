#!/usr/bin/env python3
"""Find code that loads a float/double constant at a given guest address.
Matches  lis rX,hi ; ... ; lfs/lfd fN,lo(rX)   and   lis rX,hi ; addi rY,rX,lo ; ... lfs fN,0(rY).
usage: find_const_refs.py <image> <generated init.cpp> <hex address> [...]"""
import sys, struct, re, bisect
B = 0x82000000
img = open(sys.argv[1], 'rb').read()
funcs = sorted(int(m, 16) for m in re.findall(r'\{ 0x([0-9A-F]{8}), sub_', open(sys.argv[2]).read()))
def owner(a): return funcs[bisect.bisect_right(funcs, a) - 1]
def u32(a): return struct.unpack_from('>I', img, a - B)[0]
def sx(v): return v - 0x10000 if v & 0x8000 else v
targets = [int(x, 16) for x in sys.argv[3:]]
LOADS = {48: 'lfs', 50: 'lfd', 32: 'lwz', 49: 'lfsu', 51: 'lfdu'}
for t in targets:
    print(f'== {t:08X}  (float {struct.unpack_from(">f", img, t - B)[0]:.6g})')
    for a in range(0x820C0000, 0x82336000, 4):
        w = u32(a); op = w >> 26
        if op in LOADS:
            ra = (w >> 16) & 31; d = sx(w & 0xFFFF)
            # walk back for the value of ra
            for k in range(1, 40):
                x = u32(a - 4 * k); xo = x >> 26
                if x in (0x4E800020, 0):
                    break
                if xo == 15 and (x >> 21) & 31 == ra and (x >> 16) & 31 == 0:      # lis ra,hi
                    if (((x & 0xFFFF) << 16) + d) & 0xFFFFFFFF == t:
                        print(f'   {a:08X} in sub_{owner(a):08X}: {LOADS[op]} f{(w >> 21) & 31},{d:#x}(r{ra})   [lis at {a - 4 * k:08X}]')
                    break
                if xo == 14 and (x >> 21) & 31 == ra:                               # addi ra,rb,lo
                    rb = (x >> 16) & 31; lo = sx(x & 0xFFFF)
                    for j in range(k + 1, k + 30):
                        y = u32(a - 4 * j)
                        if y in (0x4E800020, 0): break
                        if y >> 26 == 15 and (y >> 21) & 31 == rb and (y >> 16) & 31 == 0:
                            if (((y & 0xFFFF) << 16) + lo + d) & 0xFFFFFFFF == t:
                                print(f'   {a:08X} in sub_{owner(a):08X}: {LOADS[op]} f{(w >> 21) & 31},{d:#x}(r{ra})   [via addi at {a - 4 * k:08X}]')
                            break
                    break
                # any other write to ra ends the search
                if xo in (14, 15, 24, 32, 31) and (x >> 21) & 31 == ra and xo != 31:
                    break
