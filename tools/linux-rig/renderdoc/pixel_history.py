# Pixel history. Which draws touched a pixel, and what did they write?
# Run:  RDC=<file.rdc> OUT=<dir> EID=<event> TEX=<resource id> PTS="x,y x,y" qrenderdoc --python pixel_history.py
import os, traceback
import renderdoc as rd

RDC = os.environ['RDC']
OUT = os.environ['OUT']
EID = int(os.environ['EID'])
TEX = int(os.environ['TEX'])
PTS = [tuple(int(v) for v in p.split(',')) for p in os.environ['PTS'].split()]
os.makedirs(OUT, exist_ok=True)
log = open(os.path.join(OUT, 'history_%d.txt' % EID), 'w')


def P(*a):
    log.write(' '.join(str(x) for x in a) + '\n')
    log.flush()


def col(v):
    return '(%.3f %.3f %.3f %.3f)' % (v.col.floatValue[0], v.col.floatValue[1], v.col.floatValue[2], v.col.floatValue[3])


def main():
    cap = rd.OpenCaptureFile()
    P('open', cap.OpenFile(RDC, '', None))
    res, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    P('capture', res)
    rid = None
    for t in controller.GetTextures():
        if int(t.resourceId) == TEX:
            rid = t.resourceId
            P('texture', t.format.Name(), t.width, t.height)
    controller.SetFrameEvent(EID, True)
    for (x, y) in PTS:
        P('==== pixel', x, y)
        try:
            sub = rd.Subresource(0, 0, 0)
            hist = controller.PixelHistory(rid, x, y, sub, rd.CompType.Typeless)
            P('modifications', len(hist))
            for m in hist:
                flags = []
                for f in ('directShaderWrite', 'unboundPS', 'sampleMasked', 'backfaceCulled', 'depthClipped',
                          'depthBoundsFailed', 'viewClipped', 'scissorClipped', 'shaderDiscarded', 'depthTestFailed',
                          'stencilTestFailed', 'predicationSkipped'):
                    try:
                        if getattr(m, f):
                            flags.append(f)
                    except AttributeError:
                        pass
                P('  eid', m.eventId, 'prim', m.primitiveID, 'pre', col(m.preMod), 'shaderOut', col(m.shaderOut),
                  'post', col(m.postMod), 'depth %.5f' % m.shaderOut.depth, ' '.join(flags))
        except Exception:
            P('ERR', traceback.format_exc())
    controller.Shutdown()
    cap.Shutdown()


try:
    main()
except Exception:
    P('FATAL', traceback.format_exc())
log.close()
os._exit(0)
