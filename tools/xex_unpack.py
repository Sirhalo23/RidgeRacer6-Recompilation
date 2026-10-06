#!/usr/bin/env python3
"""Decrypt + decompress (basic compression only) an XEX2 into a flat memory image.
usage: xex_unpack.py default.xex out.bin   -> image loaded at the XEX load address
"""
import sys, struct
from Crypto.Cipher import AES
RETAIL_KEY = bytes.fromhex('20B185A59D28FDC340583FBB0896BF91')
d = open(sys.argv[1], 'rb').read()
assert d[:4] == b'XEX2'
_, pe_off, _, sec_off, n = struct.unpack_from('>IIIII', d, 4)
hdr = dict(struct.unpack_from('>II', d, 24 + i * 8) for i in range(n))
ff = hdr[0x3FF]
ffsz, enc, comp = struct.unpack_from('>IHH', d, ff)
img_size = struct.unpack_from('>I', d, sec_off + 4)[0]
load = struct.unpack_from('>I', d, sec_off + 0x110)[0]
data = d[pe_off:]
if enc == 1:
    session = AES.new(RETAIL_KEY, AES.MODE_ECB).decrypt(d[sec_off + 0x150:sec_off + 0x160])
    data = data[:len(data) & ~15]
    data = AES.new(session, AES.MODE_CBC, iv=bytes(16)).decrypt(data)
if comp == 1:
    out = bytearray()
    p = 0
    for o in range(ff + 8, ff + ffsz, 8):
        dsz, zsz = struct.unpack_from('>II', d, o)
        out += data[p:p + dsz]; p += dsz
        out += bytes(zsz)
    img = bytes(out)
elif comp == 0:
    img = data
else:
    raise SystemExit('LZX-compressed XEX: not handled by this script')
img = img.ljust(img_size, b'\0')[:img_size]
assert img[:2] == b'MZ', img[:16]
open(sys.argv[2], 'wb').write(img)
print(f'load {load:#010x} size {len(img):#x} -> {sys.argv[2]}')
