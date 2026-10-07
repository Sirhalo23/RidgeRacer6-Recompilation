# Lists every action of a capture with its outputs, shaders and textures;
# save the colour output whenever the output targets change and every N draws.
# Run:  RDC=<file.rdc> OUT=<dir> [EVERY=25] qrenderdoc --python list_draws.py
import os, sys, traceback
import renderdoc as rd

RDC = os.environ['RDC']
OUT = os.environ['OUT']
EVERY = int(os.environ.get('EVERY', '25'))
os.makedirs(OUT, exist_ok=True)
log = open(os.path.join(OUT, 'actions.txt'), 'w')


def P(*a):
    log.write(' '.join(str(x) for x in a) + '\n')
    log.flush()


def save(controller, rid, path, mip=0, slice_=0):
    ts = rd.TextureSave()
    ts.resourceId = rid
    ts.destType = rd.FileType.PNG
    ts.mip = mip
    ts.slice.sliceIndex = slice_
    ts.alpha = rd.AlphaMapping.Discard
    controller.SaveTexture(ts, path)


def main():
    cap = rd.OpenCaptureFile()
    res = cap.OpenFile(RDC, '', None)
    P('open', res)
    res, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    P('capture', res)
    sd = controller.GetStructuredFile()
    texs = {t.resourceId: t for t in controller.GetTextures()}
    P('textures', len(texs))

    def tdesc(rid):
        t = texs.get(rid)
        if t is None:
            return str(rid)
        return '%s[%s %dx%dx%d a%d m%d s%d %s%s]' % (
            int(rid) if hasattr(rid, '__int__') else rid, t.format.Name(), t.width, t.height, t.depth,
            t.arraysize, t.mips, t.msSamp, str(t.type).split('.')[-1], ' CUBE' if t.cubemap else '')

    acts = []

    def walk(a, depth):
        acts.append((a, depth))
        for c in a.children:
            walk(c, depth + 1)

    for r in controller.GetRootActions():
        walk(r, 0)
    P('actions', len(acts))

    last_out = None
    ndraw = 0
    for a, depth in acts:
        name = a.GetName(sd)
        flags = a.flags
        isdraw = bool(flags & rd.ActionFlags.Drawcall)
        outs = [o for o in a.outputs if o != rd.ResourceId.Null()]
        line = '%s%d %s idx=%d inst=%d' % ('  ' * depth, a.eventId, name, a.numIndices, a.numInstances)
        if isdraw:
            ndraw += 1
            try:
                controller.SetFrameEvent(a.eventId, False)
                st = controller.GetPipelineState()
                vs = st.GetShader(rd.ShaderStage.Vertex)
                ps = st.GetShader(rd.ShaderStage.Pixel)
                line += ' VS=%s PS=%s' % (vs, ps)
                line += ' out=' + ','.join(tdesc(o) for o in outs)
                if a.depthOut != rd.ResourceId.Null():
                    line += ' depth=' + tdesc(a.depthOut)
                try:
                    ro = st.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
                except TypeError:
                    ro = st.GetReadOnlyResources(rd.ShaderStage.Pixel)
                tl = []
                for u in ro:
                    try:
                        rid = u.descriptor.resource
                        idx = u.access.index
                    except AttributeError:
                        rid = u.resources[0].resourceId if u.resources else rd.ResourceId.Null()
                        idx = u.bindPoint.bind
                    if rid != rd.ResourceId.Null():
                        tl.append('%s:%s' % (idx, tdesc(rid)))
                line += ' tex={' + ' '.join(tl) + '}'
            except Exception:
                line += ' ERR ' + traceback.format_exc().replace('\n', ' | ')
            key = tuple(int(o) if hasattr(o, '__int__') else str(o) for o in outs)
            if outs and (key != last_out or ndraw % EVERY == 0):
                try:
                    controller.SetFrameEvent(a.eventId, True)
                    save(controller, outs[0], os.path.join(OUT, 'rt_%05d.png' % a.eventId))
                    line += ' [saved]'
                except Exception:
                    line += ' SAVEERR'
                last_out = key
        P(line)
    P('draws', ndraw)
    controller.Shutdown()
    cap.Shutdown()


try:
    main()
except Exception:
    P('FATAL', traceback.format_exc())
log.close()
os._exit(0)
