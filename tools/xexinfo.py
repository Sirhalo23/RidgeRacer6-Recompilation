#!/usr/bin/env python3
"""Print the interesting parts of an XEX2 header."""
import sys, struct, datetime
d = open(sys.argv[1], 'rb').read()
assert d[:4] == b'XEX2', d[:4]
flags, pe_off, _, sec_off, n = struct.unpack_from('>IIIII', d, 4)
print(f'file size        {len(d)}')
print(f'module flags     {flags:#x}')
print(f'PE data offset   {pe_off:#x}')
print(f'security offset  {sec_off:#x}')
hdr = {}
for i in range(n):
    k, v = struct.unpack_from('>II', d, 24 + i * 8)
    hdr[k] = v
def blob(k):
    v = hdr[k]
    low = k & 0xFF
    if low in (0, 1):
        return struct.pack('>I', v)
    if low == 0xFF:
        sz = struct.unpack_from('>I', d, v)[0]
        return d[v:v + sz]
    return d[v:v + low * 4]
names = {0x2FF:'resource info',0x3FF:'file format',0x405:'base reference',0x5FF:'delta patch',
 0x80FF:'bounding path',0x8105:'device id',0x10001:'original base address',0x10100:'entry point',
 0x10201:'image base',0x103FF:'import libraries',0x18002:'checksum/timestamp',0x18102:'callcap',
 0x18200:'fastcap',0x183FF:'original PE name',0x200FF:'static libraries',0x20104:'TLS info',
 0x20200:'default stack size',0x20301:'default fs cache',0x20401:'default heap size',
 0x28002:'page heap',0x30000:'system flags',0x40006:'execution info',0x401FF:'title workspace',
 0x40201:'game ratings',0x40310:'game ratings',0x40404:'LAN key',0x405FF:'xbox360 logo',
 0x406FF:'multidisc media ids',0x407FF:'alternate title ids',0x40801:'additional title memory',
 0xE10402:'exports by name'}
print('optional headers:')
for k in sorted(hdr):
    print(f'  {k:#010x} {names.get(k, "?"):26} value/offset {hdr[k]:#x}')
if 0x10100 in hdr: print(f'entry point      {hdr[0x10100]:#010x}')
if 0x10201 in hdr: print(f'image base       {hdr[0x10201]:#010x}')
if 0x20200 in hdr: print(f'stack size       {hdr[0x20200]:#x}')
if 0x30000 in hdr: print(f'system flags     {hdr[0x30000]:#x}')
if 0x183FF in hdr: print('original PE name', blob(0x183FF)[4:].rstrip(b"\0").decode())
if 0x18002 in hdr:
    cs, ts = struct.unpack('>II', blob(0x18002))
    print('timestamp       ', datetime.datetime.utcfromtimestamp(ts))
if 0x3FF in hdr:
    b = blob(0x3FF)
    enc, comp = struct.unpack_from('>HH', b, 4)
    print('encryption      ', {0:'none',1:'normal'}.get(enc, enc))
    print('compression     ', {0:'none',1:'basic',2:'normal (LZX)',3:'delta'}.get(comp, comp))
    if comp == 2:
        win = struct.unpack_from('>I', b, 8)[0]; print(f'  lzx window     {win:#x}')
if 0x40006 in hdr:
    b = blob(0x40006)
    media, ver, basever, title, plat, exe, disc, ndisc, save = struct.unpack('>IIIIBBBBI', b)
    print(f'media id         {media:08X}')
    print(f'title id         {title:08X}  ({chr(title>>24)}{chr((title>>16)&255)}-{title&0xFFFF})')
    print(f'version          {ver>>28}.{(ver>>24)&15}.{(ver>>8)&0xFFFF}.{ver&255}   base {basever:#x}')
    print(f'disc             {disc}/{ndisc}   savegame id {save:08X}')
if 0x200FF in hdr:
    b = blob(0x200FF)
    print('static libraries:')
    for o in range(4, len(b), 16):
        nm = b[o:o+8].rstrip(b'\0').decode()
        ma, mi, bu, q = struct.unpack_from('>HHHH', b, o + 8)
        print(f'  {nm:10} {ma}.{mi}.{bu}.{q & 0xFF}')
if 0x103FF in hdr:
    b = blob(0x103FF)
    strsz, nlib = struct.unpack_from('>II', b, 4)
    strs = b[12:12 + strsz].split(b'\0')
    strs = [s.decode() for s in strs if s]
    o = 12 + strsz
    print('import libraries:')
    for i in range(nlib):
        sz = struct.unpack_from('>I', b, o)[0]
        impid, ver, vmin, nameidx, cnt = struct.unpack_from('>IIIHH', b, o + 24)
        nm = strs[nameidx & 0xFF] if (nameidx & 0xFF) < len(strs) else '?'
        print(f'  {nm:16} v{ver>>28}.{(ver>>24)&15}.{(ver>>8)&0xFFFF}.{ver&255}  {cnt} records')
        o += sz
if 0x2FF in hdr:
    b = blob(0x2FF)
    print('resources:')
    for o in range(4, len(b), 16):
        nm = b[o:o+8].rstrip(b'\0').decode('latin-1')
        a, s = struct.unpack_from('>II', b, o + 8)
        print(f'  {nm:10} addr {a:#010x} size {s:#x}')
# security info
img_size = struct.unpack_from('>I', d, sec_off + 4)[0]
load_addr = struct.unpack_from('>I', d, sec_off + 0x110)[0]
print(f'image size       {img_size:#x} ({img_size/1048576:.1f} MiB)   load address {load_addr:#010x}')
img_flags = struct.unpack_from('>I', d, sec_off + 0x10C)[0]
region = struct.unpack_from('>I', d, sec_off + 0x178)[0]
print(f'image flags      {img_flags:#x}   region {region:#x}')
