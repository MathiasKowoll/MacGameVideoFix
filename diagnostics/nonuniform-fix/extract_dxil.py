#!/usr/bin/env python3
"""extract_dxil.py <container.dxbc> <out.bc>: the LLVM bitcode inside the DXIL part."""
import struct, sys
b = open(sys.argv[1], 'rb').read()
n = struct.unpack_from('<I', b, 28)[0]
for o in struct.unpack_from('<%dI' % n, b, 32):
    if b[o:o+4] == b'DXIL':
        part = b[o+8:o+8+struct.unpack_from('<I', b, o+4)[0]]
        boff, bsz = struct.unpack_from('<II', part, 16)
        open(sys.argv[2], 'wb').write(part[8+boff:8+boff+bsz]); sys.exit(0)
sys.exit('no DXIL part')
