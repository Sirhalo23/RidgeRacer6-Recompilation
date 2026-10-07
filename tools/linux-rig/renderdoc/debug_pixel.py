# Runs the pixel shader of one draw in RenderDoc's shader debugger for one
# pixel and logs every value it computes (ALL=1), which shows where a colour
# goes wrong. The SDK's shaders ask for float-controls execution modes that the
# debugger does not support, so the shader is replaced by a copy without them.
# In the log the guest registers are one array variable (r0, r1, ... in order).
# Run:  ALL=1 RDC=<file.rdc> OUT=<dir> EID=<event> PT="x,y" [PRIM=n] qrenderdoc --python debug_pixel.py
import os, struct, traceback
import renderdoc as rd

RDC = os.environ['RDC']
OUT = os.environ['OUT']
EID = int(os.environ['EID'])
X, Y = [int(v) for v in os.environ['PT'].split(',')]
PRIM = int(os.environ.get('PRIM', '-1'))
ALL = os.environ.get('ALL', '0') == '1'
os.makedirs(OUT, exist_ok=True)
log = open(os.path.join(OUT, 'debug_%d.txt' % EID), 'w')


def P(*a):
    log.write(' '.join(str(x) for x in a) + '\n')
    log.flush()


def val(v):
    if len(v.members):
        return '{' + ', '.join(val(m) for m in v.members) + '}'
    n = max(1, v.rows) * max(1, v.columns)
    t = str(v.type)
    out = []
    for i in range(n):
        if 'Float' in t or 'Half' in t or 'Double' in t:
            out.append('%.5g' % v.value.f32v[i])
        elif 'SInt' in t:
            out.append(str(v.value.s32v[i]))
        elif 'Bool' in t:
            out.append(str(bool(v.value.u32v[i])))
        else:
            out.append('0x%X' % v.value.u32v[i])
    return ' '.join(out)


def strip_float_controls(raw):
    words = list(struct.unpack('<%dI' % (len(raw) // 4), raw))
    out = words[:5]
    i = 5
    while i < len(words):
        wc = words[i] >> 16
        op = words[i] & 0xFFFF
        ins = words[i:i + wc]
        drop = False
        if op == 17 and ins[1] in (4464, 4465, 4466, 4467, 4468):
            drop = True
        if op == 16 and ins[2] in (4459, 4460, 4461, 4462, 4463):
            drop = True
        if op == 10:
            name = b''.join(struct.pack('<I', w) for w in ins[1:]).split(b'\0')[0]
            if name == b'SPV_KHR_float_controls':
                drop = True
        if not drop:
            out.extend(ins)
        i += wc
    return struct.pack('<%dI' % len(out), *out)


def main():
    cap = rd.OpenCaptureFile()
    P('open', cap.OpenFile(RDC, '', None))
    res, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    P('capture', res)
    controller.SetFrameEvent(EID, True)
    st = controller.GetPipelineState()
    refl = st.GetShaderReflection(rd.ShaderStage.Pixel)
    raw = bytes(refl.rawBytes)
    new = strip_float_controls(raw)
    P('spirv bytes', len(raw), '->', len(new))
    nid, errs = controller.BuildTargetShader(refl.entryPoint, rd.ShaderEncoding.SPIRV, new, rd.ShaderCompileFlags(), rd.ShaderStage.Pixel)
    P('build', nid, repr(errs))
    controller.ReplaceResource(refl.resourceId, nid)
    controller.SetFrameEvent(EID, True)
    inputs = rd.DebugPixelInputs()
    inputs.sample = 0
    if PRIM >= 0:
        inputs.primitive = PRIM
    trace = controller.DebugPixel(X, Y, inputs)
    if trace is None or trace.debugger is None:
        P('no trace')
        return
    P('trace stage', trace.stage, 'instructions', len(trace.instInfo) if hasattr(trace, 'instInfo') else '?')
    for v in trace.inputs:
        P('input', v.name, '=', val(v))
    steps = 0
    last = {}
    while True:
        states = controller.ContinueDebug(trace.debugger)
        if len(states) == 0:
            break
        for s in states:
            steps += 1
            for c in s.changes:
                name = c.after.name
                if not ALL and not ('xe_var_registers' in name or 'xe_out' in name or 'predicate' in name
                                    or 'previous_scalar' in name or 'tfetch' in name):
                    continue
                a = val(c.after)
                if last.get(name) == a:
                    continue
                last[name] = a
                P('step %d inst %d: %s = %s' % (steps, s.nextInstruction, name, a))
    P('steps', steps)
    controller.FreeTrace(trace)
    controller.Shutdown()
    cap.Shutdown()


try:
    main()
except Exception:
    P('FATAL', traceback.format_exc())
log.close()
os._exit(0)
