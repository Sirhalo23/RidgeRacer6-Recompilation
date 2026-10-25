#!/usr/bin/env python3
"""Prepare reviewed GPU-progress notifications in build-local source copies."""
import hashlib
import json
from pathlib import Path
import re
import sys

SDK_DIGEST = '0dc2a7068978c843ac42e612bf152de85ce7432a0b2b0c2ba18f4f83bfc216b4'
FUNCTIONS = re.compile(r'DEFINE_REX_FUNC\((sub_[A-Fa-f0-9]+)\) \{.*?(?=\nDEFINE_REX_FUNC|\Z)', re.S)
FIELDS = r'(?:10376|10384|10396|10432|10433|10488)'


def once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('Ambiguous GPU-progress anchor: ' + old[:70])
    return source.replace(old, new)


def sdk_copy(source):
    if hashlib.sha256(source.encode()).hexdigest() != SDK_DIGEST:
        raise ValueError('Unsupported SDK command processor revision; review progress writers')
    pattern = re.compile(r'(?P<indent>^[ \t]*)(?P<statement>memory::store(?:_and_swap)?(?:<uint32_t>)?\(memory_->TranslatePhysical\((?P<address>[^)]+)\),\s*[^;]+;)', re.M)
    matches = list(pattern.finditer(source))
    if len(matches) != 6:
        raise ValueError('Unexpected SDK physical word writer set: ' + str(len(matches)))
    def guarded(m):
        prefix = source[:m.start()]
        function = re.findall(r'(?:bool|void) CommandProcessor::(\w+)\(', prefix)[-1]
        kind = {'ExecutePacketType3_EVENT_WRITE_SHD': 'Event', 'ExecutePacketType3_MEM_WRITE': 'Memory',
                'ExecutePacketType3_REG_TO_MEM': 'Register', 'ExecutePacketType3_COND_WRITE': 'Conditional',
                'WriteRegister': 'Scratch', 'WorkerThreadMain': 'ReadPointer'}.get(function, 'Other')
        i = m['indent']
        return (i + '{\n' + i + '  rr6::GpuProgressWrite progress_write(memory_->TranslatePhysical(' + m['address'] + '), 4, rr6::ProgressWriter::' + kind + ');\n'
                + i + '  ' + m['statement'] + '\n' + i + '}')
    source = pattern.sub(guarded, source)
    source = once(source, '  memory::copy_and_swap_16_unaligned(memory_->TranslatePhysical(address), extents,',
                  '  rr6::GpuProgressWrite extents_write(memory_->TranslatePhysical(address), sizeof(extents), rr6::ProgressWriter::Other);\n  memory::copy_and_swap_16_unaligned(memory_->TranslatePhysical(address), extents,')
    source = once(source, '    // 0xFFFFFEED is written to this two locations',
                  '    rr6::GpuProgressWrite query_write(pSampleCounts, sizeof(*pSampleCounts), rr6::ProgressWriter::Other);\n    // 0xFFFFFEED is written to this two locations')
    source = once(source, 'void CommandProcessor::Shutdown() {\n',
                  'void CommandProcessor::Shutdown() {\n  rr6::InvalidateAllGpuProgress();\n  rr6::ReportGpuProgress();\n')
    return '#include "native_gpu_progress.h"\n' + source


def translated_function(name, body):
    # All literal writes to watched device fields publish under the bridge lock.
    body = re.sub(r'REX_STORE_U(8|32)\(([^\n]*\+ '+FIELDS+r'), ([^\n]+)\);',
                  lambda m: 'rr6::GpuProgressStore'+m[1]+'(base, '+m[2]+', '+m[3]+');', body)
    # Read the completion word consistently with physical/virtual write aliases.
    if name in ['sub_8225B038', 'sub_82262C20', 'sub_82262C00']:
        lines = body.splitlines(True)
        for i,line in enumerate(lines):
            if '+ 10384)' not in line or 'REX_LOAD_U32' not in line: continue
            register = re.search(r'ctx\.(r\d+)\.u64',line)[1]
            for j in range(i+1,min(i+14,len(lines))):
                expression = 'REX_LOAD_U32(ctx.'+register+'.u32 + 0)'
                if expression in lines[j]: lines[j] = lines[j].replace(expression, 'rr6::GpuProgressRead32(base, ctx.'+register+'.u32 + 0)')
                if re.search(r'ctx\.'+register+r'\.u64 =',lines[j]): break
        body = ''.join(lines)
    # Three audited CPU writes through the device's progress pointer.
    if name == 'sub_8225AE18':
        body = once(body, 'REX_STORE_U32(ctx.r10.u32 + 0, ctx.r7.u32);',
                    'rr6::GpuProgressStore32(base, ctx.r10.u32 + 0, ctx.r7.u32);')
    if name == 'sub_8225B8A0':
        body = once(body, 'REX_STORE_U32(ctx.r10.u32 + 0, ctx.r11.u32);',
                    'rr6::GpuProgressStore32(base, ctx.r10.u32 + 0, ctx.r11.u32);')
        body = once(body, 'DEFINE_REX_FUNC(sub_8225B8A0) {\n',
                    'DEFINE_REX_FUNC(sub_8225B8A0) {\n\trr6::InvalidateGpuProgressDevice(base, ctx.r3.u32);\n')
    if name == 'sub_82262C20':
        body = once(body, 'REX_STORE_U32(ctx.r8.u32 + 0, ctx.r11.u32);',
                    'rr6::GpuProgressStore32(base, ctx.r8.u32 + 0, ctx.r11.u32);')
        body = once(body, 'loc_82262CA0:\n', 'loc_82262CA0:\n\trr6::GpuProgressRecovery();\n')
    if name == 'sub_8225B038':
        body = once(body, 'DEFINE_REX_FUNC(sub_8225B038) {\n',
                    'DEFINE_REX_FUNC(sub_8225B038) {\n\trr6::GpuProgressScope progress_wait(base, ctx.r3.u32);\n')
        body = once(body, '\tctx.lr = 0x8225B0D8;\n\tsub_82262C20(ctx, base);',
                    '\tctx.lr = 0x8225B0D8;\n\tprogress_wait.Park(ctx.r30.u32, REX_LOAD_U32(ctx.r3.u32 + 8));\n\tsub_82262C20(ctx, base);')
    return body


def prepare(sdk, generated, output):
    manifest = json.loads(Path(__file__).with_name('native_gpu_progress_functions.json').read_text())
    sources = {name: (generated / name).read_text() for name in {v['file'] for v in manifest.values()}}
    fixture_names = ['sub_8225AFB0', 'sub_82262C00', 'sub_82262C20', 'sub_8225B038']
    fixture_originals = {}
    for name in fixture_names:
        text = (generated / manifest[name]['file']).read_text()
        fixture_originals[name] = next(m[0] for m in FUNCTIONS.finditer(text) if m[1] == name)
    found = set()
    for filename,source in sources.items():
        def transform(m):
            name = m[1]
            if name not in manifest:
                if re.search(r'REX_STORE_U(?:8|32)\([^\n]*\+ '+FIELDS+',',m[0]):
                    raise ValueError('Unreviewed device-field writer: '+name)
                return m[0]
            if manifest[name]['file'] != filename or hashlib.sha256(m[0].encode()).hexdigest() != manifest[name]['sha256']:
                raise ValueError('Unsupported translated function revision: '+name)
            found.add(name)
            return translated_function(name,m[0])
        sources[filename] = '#include "native_gpu_progress.h"\n' + FUNCTIONS.sub(transform,source)
    if found != set(manifest): raise ValueError('Missing reviewed translated functions')
    # Validate the complete literal writer set, including files outside the copies.
    for p in generated.glob('*recomp.*.cpp'):
        if p.name in sources: continue
        for m in FUNCTIONS.finditer(p.read_text()):
            if re.search(r'REX_STORE_U(?:8|32)\([^\n]*\+ '+FIELDS+',',m[0]):
                raise ValueError('Unreviewed translated writer: '+m[1])
    common = sdk_copy((sdk/'src/graphics/command_processor.cpp').read_text())
    output.mkdir(parents=True, exist_ok=True)
    def write_changed(path, text):
        if not path.exists() or path.read_text() != text:
            path.write_text(text)
    for filename,source in sources.items(): write_changed(output/filename,source)
    write_changed(output/'command_processor.cpp',common)
    write_changed(output/'sources.cmake','set(native_progress_generated\n'+''.join('    "'+str((output/name).resolve())+'"\n' for name in sorted(sources))+')\n')
    fixture = '#include "gpu_progress_fixture.h"\n#include "native_gpu_progress.h"\n'
    for namespace, native in [('original_progress', False), ('native_progress', True)]:
        fixture += '\nnamespace '+namespace+' {\n'
        for name in fixture_names:
            fixture += (translated_function(name, fixture_originals[name]) if native else fixture_originals[name]) + '\n'
        fixture += '}\n'
    write_changed(output/'gpu_progress_fixture.cpp', fixture)


if __name__ == '__main__':
    try: prepare(*map(Path,sys.argv[1:]))
    except (ValueError,OSError) as error: sys.exit(str(error))
