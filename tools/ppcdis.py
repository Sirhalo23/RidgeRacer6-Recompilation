#!/usr/bin/env python3
"""PowerPC disassembly of the unpacked image.  usage: ppcdis.py <start_hex> [count] [image]"""
import sys, struct
from capstone import Cs, CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN
BASE = 0x82000000
img = open(sys.argv[3] if len(sys.argv) > 3 else 'rr6-recomp/analysis/default.bin', 'rb').read()
md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
a = int(sys.argv[1], 16); n = int(sys.argv[2]) if len(sys.argv) > 2 else 24
for i in range(n):
    w = img[a - BASE:a - BASE + 4]
    ins = next(md.disasm(w, a), None)
    print(f'{a:08X}  {w.hex()}  ' + (f'{ins.mnemonic} {ins.op_str}' if ins else '.long'))
    a += 4
