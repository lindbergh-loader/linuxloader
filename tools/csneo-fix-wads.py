#!/usr/bin/env python3
"""Rebuild the truncated WAD3 directories of a Counter Strike NEO dump.

Some contents2.bin extractions leave the GoldSrc wads (valve/ gfx, fonts,
decals, halflife, xeno; czero/ decals, hlbasics, liquids, ...) cut short:
every lump is intact, but only the first half of the lump directory survives
(file length = dir_offset + n*16 + 4, the last 4 bytes being the next file's
"WAD3" magic). The engine then reads uninitialised directory entries and
corrupts its heap in Decal_Init / dies with "W_GetLumpinfo: paused not found".

Lumps are stored back to back from offset 12, so the missing entries are
recovered by walking the lump data: miptex lumps (decals/textures) carry their
own name and size; gfx.wad / fonts.wad use the known stock layout.

Usage: csneo-fix-wads.py <csneo2/linux>   (keeps <wad>.truncated backups)
"""
import os
import struct
import sys

ENTRY = struct.Struct('<iiibbh16s')


def is_miptex(d, pos):
    if pos + 40 > len(d):
        return False
    w, h = struct.unpack_from('<II', d, pos + 16)
    return (struct.unpack_from('<I', d, pos + 24)[0] == 40 and 0 < w <= 4096 and 0 < h <= 4096
            and w % 8 == 0 and h % 8 == 0 and d[pos] >= 0x20)


def miptex_size(d, pos, end):
    # Standard lump: header + 4 mips + 256-colour palette, padded to 4.
    # A few textures carry a longer palette block, so when the next lump
    # does not start right after, scan forward for its header.
    w, h = struct.unpack_from('<II', d, pos + 16)
    size = (40 + w * h * 85 // 64 + 2 + 768 + 3) & ~3
    if pos + size == end or is_miptex(d, pos + size):
        return size
    for nxt in range(pos + 40 + w * h * 85 // 64, end):
        if is_miptex(d, nxt):
            return nxt - pos
    return end - pos


def miptex_name(d, pos):
    return d[pos:pos + 16].split(b'\0')[0]


# Stock layouts for the non-miptex wads, as (name, type, size or None = up to
# the directory) for the entries past the ones that survived.
KNOWN = {
    'gfx.wad': [(b'PAUSED', 0x42, None), (b'CONCHARS', 0x46, 34642),
                (b'UNKNOWN5', 0x40, None), (b'BACKTILE', 0x42, 4876)],
    'fonts.wad': [(b'FONT1', 0x46, 34642), (b'FONT2', 0x46, 34642)],
}


def fix(path):
    d = open(path, 'rb').read()
    magic, n, off = struct.unpack_from('<4sii', d)
    if magic != b'WAD3' or off + n * 32 <= len(d):
        return False
    entries = []
    for i in range(n):
        e = d[off + i * 32:off + i * 32 + 32]
        if len(e) < 32:
            break
        entries.append(list(ENTRY.unpack(e)))
    # The first 16 bytes (pos/sizes/type) of the next entry survive too.
    partial = d[off + len(entries) * 32:off + len(entries) * 32 + 16]
    first_missing = len(entries)
    pos = entries[-1][0] + entries[-1][1] if entries else 12
    base = os.path.basename(path).lower()
    known = KNOWN.get(base)
    k = 0
    while len(entries) < n:
        if len(entries) == first_missing and len(partial) == 16:
            ppos, dsz, sz, typ = struct.unpack_from('<iiib', partial)
            assert ppos == pos, (path, ppos, pos)
        else:
            dsz = typ = None
        if known:
            name, ktyp, ksz = known[k]
            k += 1
            if dsz is None:
                remaining = n - len(entries) - 1
                tail = sum(s for _, _, s in known[k:k + remaining] if s)
                dsz = ksz if ksz else off - pos - tail
                typ = ktyp
        else:
            name = miptex_name(d, pos)
            if dsz is None:
                dsz = miptex_size(d, pos, off)
                typ = 0x43
        entries.append([pos, dsz, dsz, typ, 0, 0, name.ljust(16, b'\0')])
        pos += dsz
    if pos != off:
        raise SystemExit(f'{path}: lumps end at {pos}, directory at {off}; not rebuilt')
    os.rename(path, path + '.truncated')
    with open(path, 'wb') as f:
        f.write(d[:off])
        for e in entries:
            f.write(ENTRY.pack(*e))
    print(f'{path}: rebuilt {n} entries')
    return True


root = sys.argv[1] if len(sys.argv) > 1 else '.'
for sub in ('valve', 'czero', 'cstrike'):
    dirp = os.path.join(root, sub)
    if os.path.isdir(dirp):
        for fn in sorted(os.listdir(dirp)):
            if fn.lower().endswith('.wad'):
                fix(os.path.join(dirp, fn))
