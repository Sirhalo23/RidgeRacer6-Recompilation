#!/usr/bin/env python3
"""Find code that forms an address inside [lo, hi): lis rX,hi16 followed (within a few
instructions) by addi / load / store using rX with a 16-bit displacement.
usage: find_addr_refs.py <image> <init.cpp> <lo hex> <hi hex>"""
import sys, struct, re, bisect
B = 0x82000000
img = open(sys.argv[1], 'rb').read()
funcs = sorted(int(m, 16) for m in re.findall(r'\{ 0x([0-9A-F]{8}), sub_', open(sys.argv[2]).read()))
lo, hi = int(sys.argv[3], 16), int(sys.argv[4], 16)
def owner(a): return funcs[bisect.bisect_right(funcs, a) - 1]
def u32(a): return struct.unpack_from('>I', img, a - B)[0]
def sx(v): return v - 0x10000 if v & 0x8000 else v
NAMES = {14: 'addi', 32: 'lwz', 34: 'lbz', 40: 'lhz', 42: 'lha', 36: 'stw', 38: 'stb', 44: 'sth',
         48: 'lfs', 50: 'lfd', 52: 'stfs', 54: 'stfd', 58: 'ld', 62: 'std'}
his = {(lo >> 16) & 0xFFFF, ((lo >> 16) + 1) & 0xFFFF, (hi >> 16) & 0xFFFF, ((hi >> 16) + 1) & 0xFFFF}
for a in range(0x820C0000, 0x82336000, 4):
    w = u32(a)
    if w >> 26 == 15 and (w >> 16) & 31 == 0 and (w & 0xFFFF) in his:
        rd = (w >> 21) & 31; base = (w & 0xFFFF) << 16
        for k in range(1, 10):
            x = u32(a + 4 * k); op = x >> 26
            if x in (0x4E800020, 0): break
            if op in NAMES and (x >> 16) & 31 == rd:
                t = (base + sx(x & 0xFFFF)) & 0xFFFFFFFF
                if lo <= t < hi:
                    print(f'{a + 4 * k:08X} in sub_{owner(a):08X}: {NAMES[op]} r/f{(x >> 21) & 31},{sx(x & 0xFFFF):#x}(r{rd}) -> {t:08X}')
            if op in (14, 15) and (x >> 21) & 31 == rd: break
