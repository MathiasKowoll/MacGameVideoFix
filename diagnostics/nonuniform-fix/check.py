#!/usr/bin/env python3
"""check.py <original.dxbc> <rewritten.dxbc>: exit 0 only when the input and
output signatures and the PSV0 resource bindings are identical."""
import struct, sys
def parts(b):
    n = struct.unpack_from('<I', b, 28)[0]
    offs = struct.unpack_from('<%dI' % n, b, 32)
    return {b[o:o+4].decode('latin1'): b[o+8:o+8+struct.unpack_from('<I', b, o+4)[0]] for o in offs}
def res(p):
    info = struct.unpack_from('<I', p, 0)[0]; o = 4 + info
    cnt = struct.unpack_from('<I', p, o)[0]
    if not cnt: return []
    bsz = struct.unpack_from('<I', p, o + 4)[0]; o += 8
    return sorted(tuple(struct.unpack_from('<6I', p, o + i * bsz)[:5]) for i in range(cnt))
a = parts(open(sys.argv[1], 'rb').read()); b = parts(open(sys.argv[2], 'rb').read())
ok = a['ISG1'] == b['ISG1'] and a['OSG1'] == b['OSG1'] and res(a['PSV0']) == res(b['PSV0']) and 'HASH' in b
print('ISG1 %s  OSG1 %s  resources %s  signed %s' % (a['ISG1'] == b['ISG1'], a['OSG1'] == b['OSG1'],
      res(a['PSV0']) == res(b['PSV0']), 'HASH' in b))
sys.exit(0 if ok else 1)
