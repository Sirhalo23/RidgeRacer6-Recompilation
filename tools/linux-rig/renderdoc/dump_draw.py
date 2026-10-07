# For chosen events, saves the textures the pixel shader reads (colour
# and alpha separately), the output after the draw, the shader disassembly and
# the values of its constant blocks.
# Run:  RDC=<file.rdc> OUT=<dir> EIDS="29 853" [DISASM=1] qrenderdoc --python dump_draw.py
import os, traceback
import renderdoc as rd

RDC = os.environ['RDC']
OUT = os.environ['OUT']
EIDS = [int(x) for x in os.environ['EIDS'].split()]
DISASM = os.environ.get('DISASM', '1') == '1'
os.makedirs(OUT, exist_ok=True)
log = open(os.path.join(OUT, 'detail.txt'), 'w')


def P(*a):
    log.write(' '.join(str(x) for x in a) + '\n')
    log.flush()


def save(controller, rid, path, alpha, mip=0, slice_=0):
    ts = rd.TextureSave()
    ts.resourceId = rid
    ts.destType = rd.FileType.PNG
    ts.mip = mip
    ts.slice.sliceIndex = slice_
    if alpha:
        ts.channelExtract = 3
        ts.alpha = rd.AlphaMapping.Discard
    else:
        ts.alpha = rd.AlphaMapping.Discard
    return controller.SaveTexture(ts, path)


def dump_vars(vars_, indent):
    for v in vars_:
        if len(v.members):
            P('%s%s:' % (indent, v.name))
            dump_vars(v.members, indent + '  ')
        else:
            vals = []
            n = max(1, v.rows) * max(1, v.columns)
            t = str(v.type)
            for i in range(n):
                if 'Float' in t or 'Double' in t or 'Half' in t:
                    vals.append('%.6g' % v.value.f32v[i])
                elif 'SInt' in t:
                    vals.append(str(v.value.s32v[i]))
                else:
                    vals.append('0x%X' % v.value.u32v[i])
            P('%s%s = %s' % (indent, v.name, ' '.join(vals)))


def main():
    cap = rd.OpenCaptureFile()
    P('open', cap.OpenFile(RDC, '', None))
    res, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    P('capture', res)
    texs = {t.resourceId: t for t in controller.GetTextures()}

    def tdesc(rid):
        t = texs.get(rid)
        if t is None:
            return str(rid)
        return '%d[%s %dx%dx%d a%d m%d s%d %s%s]' % (int(rid), t.format.Name(), t.width, t.height, t.depth,
                                                      t.arraysize, t.mips, t.msSamp, str(t.type).split('.')[-1],
                                                      ' CUBE' if t.cubemap else '')

    for eid in EIDS:
        P('==== event', eid)
        controller.SetFrameEvent(eid, True)
        st = controller.GetPipelineState()
        pipe = st.GetGraphicsPipelineObject()
        for stage, sname in ((rd.ShaderStage.Vertex, 'vs'), (rd.ShaderStage.Pixel, 'ps')):
            refl = st.GetShaderReflection(stage)
            if refl is None:
                P(sname, 'none')
                continue
            P(sname, 'shader', st.GetShader(stage), 'entry', st.GetShaderEntryPoint(stage))
            try:
                ro = st.GetReadOnlyResources(stage, True)
                for u in ro:
                    rid = u.descriptor.resource
                    if rid == rd.ResourceId.Null():
                        continue
                    idx = u.access.index
                    P('  tex', idx, 'arr', u.access.arrayElement, tdesc(rid), 'mip', u.descriptor.firstMip, 'slice',
                      u.descriptor.firstSlice)
                    if sname == 'ps':
                        t = texs.get(rid)
                        nsl = 1
                        if t is not None and (t.cubemap or t.arraysize > 1):
                            nsl = min(t.arraysize, 6)
                        for sl in range(nsl):
                            base = os.path.join(OUT, 'e%05d_tex%d_%d_s%d' % (eid, idx, int(rid), sl))
                            save(controller, rid, base + '_rgb.png', False, 0, sl)
                            save(controller, rid, base + '_a.png', True, 0, sl)
            except Exception:
                P('  tex ERR', traceback.format_exc())
            try:
                for i, cb in enumerate(refl.constantBlocks):
                    bind = st.GetConstantBlock(stage, i, 0)
                    d = bind.descriptor
                    vars_ = controller.GetCBufferVariableContents(pipe, refl.resourceId, stage, refl.entryPoint, i,
                                                                  d.resource, d.byteOffset, d.byteSize)
                    P('  cblock', i, cb.name, 'size', cb.byteSize)
                    dump_vars(vars_, '    ')
            except Exception:
                P('  cb ERR', traceback.format_exc())
            if DISASM:
                try:
                    targets = controller.GetDisassemblyTargets(True)
                    P('  disasm targets', list(targets))
                    tgt = targets[0]
                    for cand in targets:
                        if 'GLSL' in cand:
                            tgt = cand
                            break
                    text = controller.DisassembleShader(pipe, refl, tgt)
                    open(os.path.join(OUT, 'e%05d_%s.txt' % (eid, sname)), 'w').write(text)
                except Exception:
                    P('  disasm ERR', traceback.format_exc())
        outs = st.GetOutputTargets()
        for i, o in enumerate(outs):
            rid = o.resource
            if rid == rd.ResourceId.Null():
                continue
            P('  out', i, tdesc(rid))
            base = os.path.join(OUT, 'e%05d_out%d_%d' % (eid, i, int(rid)))
            save(controller, rid, base + '_rgb.png', False)
            save(controller, rid, base + '_a.png', True)
        try:
            cbs = st.GetColorBlends()
            for i, b in enumerate(cbs[:len(outs)]):
                P('  blend', i, 'enabled', b.enabled, 'mask', b.writeMask, 'src', b.colorBlend.source, 'dst',
                  b.colorBlend.destination, 'op', b.colorBlend.operation, 'asrc', b.alphaBlend.source, 'adst',
                  b.alphaBlend.destination)
        except Exception:
            P('  blend ERR', traceback.format_exc())
    controller.Shutdown()
    cap.Shutdown()


try:
    main()
except Exception:
    P('FATAL', traceback.format_exc())
log.close()
os._exit(0)
