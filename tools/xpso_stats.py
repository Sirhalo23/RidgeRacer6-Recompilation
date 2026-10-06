#!/usr/bin/env python3
"""Summarises a ReXGlue / Xenia Direct3D 12 pipeline storage file (*.d3d12.xpso).

usage: xpso_stats.py <file.xpso> [vertex shader hash, hex]

For every vertex/pixel shader pair it prints how many pipelines are stored, and
for the pair with the most (or the given vertex shader) how many different
values each field of the pipeline description takes. A field with thousands of
values is the state the game keeps changing.

Layout (rex/graphics/d3d12/pipeline_cache.h, PipelineDescription version
0x20210425, little-endian, packed): 12-byte file header, then 72-byte records:
u64 description hash, u64 VS hash, u64 VS modification, u64 PS hash,
u64 PS modification, i32 depth_bias, f32 depth_bias_slope_scaled,
u32 state bits, u32 stencil bits, 4 x u32 render target.
"""
import collections
import struct
import sys

RECORD = struct.Struct('<QQQQQifII4I')

STATE_BITS = [  # (name, width), lowest bits first
    ('strip_cut_index', 2), ('topology_or_tessellation', 2), ('geometry_shader', 2),
    ('fill_wireframe', 1), ('cull_mode', 2), ('front_ccw', 1), ('depth_clip', 1),
    ('host_msaa_samples', 2), ('depth_format', 1), ('depth_func', 3), ('depth_write', 1),
    ('stencil_enable', 1), ('stencil_read_mask', 8),
]
STENCIL_BITS = [
    ('stencil_write_mask', 8), ('front_fail_op', 3), ('front_depth_fail_op', 3),
    ('front_pass_op', 3), ('front_func', 3), ('back_fail_op', 3), ('back_depth_fail_op', 3),
    ('back_pass_op', 3), ('back_func', 3),
]


def split_bits(value, layout):
    out, shift = {}, 0
    for name, width in layout:
        out[name] = (value >> shift) & ((1 << width) - 1)
        shift += width
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    data = open(sys.argv[1], 'rb').read()
    magic, api, version = struct.unpack_from('<4s4sI', data, 0)
    count = (len(data) - 12) // RECORD.size
    print(f'{sys.argv[1]}: magic {magic!r} api {api!r}, {count} pipelines')
    if (len(data) - 12) % RECORD.size:
        print('  note: file size is not a whole number of records; layout may differ')
    records = [RECORD.unpack_from(data, 12 + i * RECORD.size) for i in range(count)]
    pairs = collections.Counter((r[1], r[3]) for r in records)
    print('pipelines per shader pair (top 8):')
    for (vs, ps), n in pairs.most_common(8):
        print(f'  VS {vs:016X} PS {ps:016X}: {n}')
    if not pairs:
        return
    if len(sys.argv) > 2:
        wanted = int(sys.argv[2], 16)
        chosen = [r for r in records if r[1] == wanted]
    else:
        (vs, ps), _ = pairs.most_common(1)[0]
        chosen = [r for r in records if r[1] == vs and r[3] == ps]
    print(f'\nfields of the {len(chosen)} pipelines of VS {chosen[0][1]:016X}:')
    fields = collections.defaultdict(collections.Counter)
    for r in chosen:
        fields['vs_modification'][hex(r[2])] += 1
        fields['ps_modification'][hex(r[4])] += 1
        fields['depth_bias'][r[5]] += 1
        fields['depth_bias_slope_scaled'][r[6]] += 1
        for k, v in split_bits(r[7], STATE_BITS).items():
            fields[k][v] += 1
        for k, v in split_bits(r[8], STENCIL_BITS).items():
            fields[k][v] += 1
        for i in range(4):
            fields[f'render_target_{i}'][hex(r[9 + i])] += 1
    for name, values in sorted(fields.items(), key=lambda kv: -len(kv[1])):
        sample = ', '.join(str(v) for v, _ in values.most_common(6))
        print(f'  {name:26s} {len(values):7d} different values   e.g. {sample}')


if __name__ == '__main__':
    main()
