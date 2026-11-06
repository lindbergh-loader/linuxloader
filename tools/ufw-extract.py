#!/usr/bin/env python3
"""List / extract Ultra Loader .ufw firmware containers (ULTRAFW v1)."""
import struct, sys, zlib, hashlib, os
f = open(sys.argv[1], 'rb').read()
out = sys.argv[2] if len(sys.argv) > 2 else None
assert f[:8] == b'ULTRAFW\0'
ver, n = struct.unpack_from('<HH', f, 8)
print('ver', ver, 'count?', n, 'hdr', f[0x10:0x70].hex())
off = 0x70
i = 0
while off + 0x30 <= len(f):
    t, v, o, cs, rs = struct.unpack_from('<HHIII', f, off)
    if o < off or o + cs > len(f) or t > 0x100:
        break
    h = f[off+0x10:off+0x30]
    blob = f[o:o+cs]
    kind = 'zlib' if blob[:1] == b'\x78' else 'raw'
    print(f'#{i} type=0x{t:x} v={v} off=0x{o:x} csize=0x{cs:x} rsize=0x{rs:x} {kind} head={blob[:16].hex()} sha256ok={hashlib.sha256(blob).digest()==h}')
    if out:
        os.makedirs(out, exist_ok=True)
        data = blob
        if kind == 'zlib':
            try: data = zlib.decompress(blob)
            except Exception as e: print('  zlib fail', e)
        open(f'{out}/entry{i}_type{t:02x}.bin', 'wb').write(data)
        print('  ->', len(data), hashlib.sha256(data).digest() == h)
    off += 0x30; i += 1
    if off >= 0x160 and off >= min(struct.unpack_from('<I', f, 0x74)[0], len(f)): break
