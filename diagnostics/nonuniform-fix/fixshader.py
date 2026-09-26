#!/usr/bin/env python3
"""
fixshader.py -- rewrite a DXIL pixel shader so that every non-uniform texture
read is served one distinct index at a time.

D3DMetal (4.0b2, with Metal 4 on and off) executes a texture read indexed with
NonUniformResourceIndex as if the index were uniform: one lane's index is used
for the whole SIMD group. Control Resonant's window interiors (interiorPS) came
out as flat, wrong colours that "cloned" onto neighbouring windows; wrapping the
reads in a bounded WaveReadLaneFirst loop fixed them.

Pipeline, all offline:
    DXBC --dxil-spirv--> SPIR-V --spirv-cross--> HLSL --this--> HLSL --dxc--> DXBC

What this script changes in spirv-cross's HLSL:
  * buffers: dxil-spirv reads structured and raw buffers as word-addressed
    Buffer<uint4>. They are redeclared with the types the title's views have
    (StructuredBuffer of the original stride, ByteAddressBuffer), read through
    Ln(word) helpers that return the word in .x -- the only component the
    generated code ever reads (asserted).
  * input/output signatures: rebuilt from the DXIL metadata, in the original
    order, with the original semantics, indices, types and interpolation.
  * every `T VAR = ARR[NonUniformResourceIndex(I)].M(...)`: a bounded loop over
    the distinct indices of the group. Sample/SampleBias become SampleGrad with
    derivatives taken before the loop.

The caller compiles the result with dxc and must compare ISG1, OSG1 and the
PSV0 resource list with the original before using it (check.py does).

Usage: fixshader.py <original.ll> <spirv-cross.hlsl> <out.hlsl>
SPDX-License-Identifier: GPL-3.0-or-later
"""
import re
import sys

ll_path, hlsl_path, out_path = sys.argv[1:4]
ll = open(ll_path).read()
src = open(hlsl_path).read()

# ---- metadata -----------------------------------------------------------------
md = {}
for m in re.finditer(r'^!(\d+) = (?:distinct )?!\{(.*)\}$', ll, re.M):
    md[int(m.group(1))] = m.group(2)


def refs(s):
    return [int(x) for x in re.findall(r'!(\d+)', s)]


def ints(s):
    return [int(x) for x in re.findall(r'i(?:8|32|64) (-?\d+)', s)]


# !dx.resources = !{!N}; !N = !{SRVs, UAVs, CBVs, Samplers}
res_root = int(re.search(r'^!dx\.resources = !\{!(\d+)\}', ll, re.M).group(1))
srv_list, uav_list = [x.strip() for x in md[res_root].split(',')][:2]
if uav_list != 'null':
    sys.exit('refusing: the shader writes UAVs')
srvs = []
if srv_list != 'null':
    for r in refs(md[int(srv_list[1:])]):
        body = md[r]
        nums = ints(body.split('!""')[1])       # space, lower bound, range, kind, ...
        space, lower, _range, kind = nums[0], nums[1], nums[2], nums[3]
        stride = None
        tag = refs(body.split('!""')[1])
        if tag:
            t = ints(md[tag[0]])
            if len(t) >= 2 and t[0] == 1:
                stride = t[1]
        srvs.append(dict(space=space, reg=lower, kind=kind, stride=stride))

# signatures: the entry point's second operand is {inputs, outputs, patch}
entry = int(re.search(r'^!dx\.entryPoints = !\{!(\d+)\}', ll, re.M).group(1))
sig_ref = refs(md[entry])[0]
sig_in, sig_out = [x.strip() for x in md[sig_ref].split(',')][:2]


def elements(listref):
    out = []
    if listref == 'null':
        return out
    for e in refs(md[int(listref[1:])]):
        body = md[e]
        name = re.search(r'!"([^"]+)"', body).group(1)
        after = body.split('!"%s"' % name, 1)[1]
        n = ints(after)          # comptype, semantic kind, [idx list is a ref], interp, rows, cols, row, col
        idx = ints(md[refs(after)[0]])
        comptype, semkind, interp, rows, cols = n[0], n[1], n[2], n[3], n[4]
        out.append(dict(name=name, idx=idx[0], comp=comptype, sem=semkind, interp=interp, cols=cols))
    return out


ins, outs = elements(sig_in), elements(sig_out)

# ---- buffers --------------------------------------------------------------------
decl_re = re.compile(r'^Buffer<uint4> (_\d+) : register\(t(\d+), space(\d+)\);$', re.M)
helpers = []
for m in decl_re.finditer(src):
    var, reg, space = m.group(1), int(m.group(2)), int(m.group(3))
    r = next((s for s in srvs if s['reg'] == reg and s['space'] == space), None)
    if r is None:
        sys.exit('no metadata for %s' % var)
    for use in re.findall(r'\b%s\.Load\([^;]*?\)\.([xyzw]+)' % var, src):
        if use != 'x':
            sys.exit('%s read beyond .x' % var)
    for v in re.findall(r'uint4 (_\d+) = %s\.Load' % var, src):
        if set(re.findall(r'\b%s\.([xyzw]+)' % v, src)) - {'x'}:
            sys.exit('%s read beyond .x' % v)
    name = 'T' + var
    if r['kind'] == 11:                                     # ByteAddressBuffer
        decl = 'ByteAddressBuffer %s : register(t%d, space%d);' % (name, reg, space)
        helpers.append('uint4 L%s(uint k) { return uint4(%s.Load(k << 2), 0u, 0u, 0u); }' % (var, name))
    elif r['kind'] == 12:                                   # StructuredBuffer
        words = r['stride'] // 4
        if words == 1:
            decl = 'StructuredBuffer<uint> %s : register(t%d, space%d);' % (name, reg, space)
            helpers.append('uint4 L%s(uint k) { return uint4(%s[k], 0u, 0u, 0u); }' % (var, name))
        else:
            decl = ('struct W%s { uint w[%d]; };\nStructuredBuffer<W%s> %s : register(t%d, space%d);'
                    % (var, words, var, name, reg, space))
            helpers.append('uint4 L%s(uint k) { return uint4(%s[k / %du].w[k %% %du], 0u, 0u, 0u); }'
                           % (var, name, words, words))
    else:
        continue                                            # a real typed buffer: leave it
    src = src.replace(m.group(0), decl)
    src = re.sub(r'\b%s\.Load\(' % var, 'L%s(' % var, src)

# ---- non-uniform texture reads ---------------------------------------------------
# Every `ARR[NonUniformResourceIndex(I)].M(ARGS)` in a line is pulled out into a
# temporary computed by a bounded loop just before that line; the expression then
# uses the temporary. Works wherever the read sits: a declaration, an assignment,
# a comparison.
elem_type = {m.group(2): m.group(1) for m in
             re.finditer(r'^Texture\w*<(\w+)> (_\d+)\[\]', src, re.M)}
KEY = '[NonUniformResourceIndex('


def split_args(argstr):
    args, depth, cur = [], 0, ''
    for c in argstr:
        if c == ',' and depth == 0:
            args.append(cur.strip()); cur = ''; continue
        depth += {'(': 1, ')': -1}.get(c, 0)
        cur += c
    args.append(cur.strip())
    return args


count = 0
lines = src.split('\n')
for li, line in enumerate(lines):
    if KEY not in line:
        continue
    ind = re.match(r'\s*', line).group(0)
    pre = []
    while KEY in line:
        j = line.index(KEY)
        arr = re.search(r'(_\d+)$', line[:j]).group(1)
        start = j - len(arr)
        k = j + len(KEY); depth = 1; s0 = k
        while depth:
            depth += {'(': 1, ')': -1}.get(line[k], 0); k += 1
        idx = line[s0:k - 1]
        m = re.match(r'\]\.(\w+)\(', line[k:])
        if not m:
            sys.exit('unhandled form: ' + line.strip())
        method = m.group(1)
        a0 = k + m.end(); k = a0; depth = 1
        while depth:
            depth += {'(': 1, ')': -1}.get(line[k], 0); k += 1
        args = split_args(line[a0:k - 1])
        ty = elem_type.get(arr)
        if ty is None:
            sys.exit('no element type for ' + arr)
        t = '_nu%d' % count
        if method == 'Sample':
            pre.append('float2 _uv%s = %s;' % (t, args[1]))
            pre.append('float2 _dx%s = ddx(_uv%s), _dy%s = ddy(_uv%s);' % (t, t, t, t))
            call = 'SampleGrad(%s, _uv%s, _dx%s, _dy%s)' % (args[0], t, t, t)
        elif method in ('SampleLevel', 'Load', 'SampleGrad'):
            call = '%s(%s)' % (method, ', '.join(args))
        else:
            sys.exit('unhandled method ' + method)
        pre += ['%s %s = (%s)0;' % (ty, t, ty),
                '{',
                '    uint _i%s = %s;' % (t, idx),
                '    [loop] for (uint _k%s = 0u; _k%s < 64u; _k%s++)' % (t, t, t),
                '    {',
                '        uint _f%s = WaveReadLaneFirst(_i%s);' % (t, t),
                '        [branch] if (_f%s == _i%s) { %s = %s[_f%s].%s; break; }' % (t, t, t, arr, t, call),
                '    }',
                '}']
        line = line[:start] + t + line[k:]
        count += 1
    lines[li] = '\n'.join(ind + p for p in pre) + '\n' + line
src = '\n'.join(lines)
if 'NonUniformResourceIndex' in src:
    sys.exit('a non-uniform read was left unrewritten')

# ---- signatures -------------------------------------------------------------------
def hlsl_type(e):
    if e['sem'] == 13:                       # SV_IsFrontFace
        return 'bool'
    base = {9: 'float', 5: 'uint', 4: 'int', 8: 'half'}.get(e['comp'])
    if base is None:
        sys.exit('component type %d' % e['comp'])
    return base if e['cols'] == 1 else '%s%d' % (base, e['cols'])


INTERP = {0: '', 1: 'nointerpolation ', 2: '', 3: 'centroid ', 4: 'noperspective ',
          5: 'noperspective centroid ', 6: 'sample ', 7: 'noperspective sample '}


# spirv-cross's own names for the system values it declares.
SV_FIELD = {'SV_Position': 'gl_FragCoord', 'SV_IsFrontFace': 'gl_FrontFacing',
            'SV_SampleIndex': 'gl_SampleID', 'SV_PrimitiveID': 'gl_PrimitiveID',
            'SV_Coverage': 'gl_SampleMaskIn'}


def field(e):
    if e['name'] in SV_FIELD:
        return SV_FIELD[e['name']]
    return e['name'] if e['idx'] == 0 else '%s_%d' % (e['name'], e['idx'])


def semantic(e):
    if e['name'].startswith('SV_') and e['name'] != 'SV_Target':
        return e['name']
    return '%s%d' % (e['name'], e['idx'])


def interp(e):
    if e['name'] == 'SV_Position' or e['sem'] == 13:
        return ''
    return INTERP.get(e['interp'], '')


fields_in = ['    %s%s %s : %s;' % (interp(e), hlsl_type(e), field(e), semantic(e)) for e in ins]
fields_out = ['    %s %s : %s;' % (hlsl_type(e), field(e), semantic(e)) for e in outs]
src = re.sub(r'struct SPIRV_Cross_Input\s*\{[^}]*\};',
             'struct SPIRV_Cross_Input\n{\n%s\n};' % '\n'.join(fields_in), src)
src = re.sub(r'struct SPIRV_Cross_Output\s*\{[^}]*\};',
             'struct SPIRV_Cross_Output\n{\n%s\n};' % '\n'.join(fields_out), src)
if helpers:
    # After every declaration they read, before the first use.
    src = src.replace('struct SPIRV_Cross_Input', '\n'.join(helpers) + '\n\nstruct SPIRV_Cross_Input', 1)
open(out_path, 'w').write(src)
print('rewrote %d non-uniform read(s), %d buffer(s)' % (count, len(helpers)))
