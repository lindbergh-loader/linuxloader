#!/usr/bin/env python3
"""Power Truck S's game (TeknoRaven's pt) as a standard ELF.

TeknoRaven's package ships pt rebuilt for its own Windows loader ("HASPE
linux loader"): of its 1430 PLT imports only a handful keep their names
and relocations, the GOT holds 0xf000f000, the stubs push 0xdeadbeef, and
the symbols the game takes the address of point into a table of its own.
The game's code and data are IGS's, untouched. The imports' names
(tools/igs-pts-imports.txt) were matched against Power Truck's pt, the
same code base with its symbols: call sites, the PLT's order, and the
callers of the few new ones.

This appends to a copy of pt a segment with the dynamic symbols (theirs and
the missing imports), their strings, versions and SysV hash, and a
.rel.plt for every stub, and points the dynamic section at them; the stubs
push their relocation again and the GOT holds their lazy-binding entries.

usage: igs-pts-repair.py <TeknoRaven's pt> <repaired pt>
"""
import os
import re
import struct
import sys
import zlib

FILE_CRC = 0xAB3DA4E1
GOT_FIRST = 0x0A11C9E4  # the PLT's first slot
GMON_SLOT = 0x0A11C9D4  # __gmon_start__ (weak): 0, not called

DT = {
    'PLTRELSZ': 2, 'HASH': 4, 'STRTAB': 5, 'SYMTAB': 6, 'STRSZ': 10,
    'JMPREL': 0x17, 'VERSYM': 0x6FFFFFF0, 'VERNEED': 0x6FFFFFFE,
    'VERNEEDNUM': 0x6FFFFFFF,
}


def elf_hash(name):
    h = 0
    for c in name:
        h = ((h << 4) + c) & 0xFFFFFFFF
        g = h & 0xF0000000
        if g:
            h ^= g >> 24
        h &= ~g & 0xFFFFFFFF
    return h


class Image:
    def __init__(self, data):
        self.d = bytearray(data)
        self.phoff = struct.unpack_from('<I', self.d, 28)[0]
        self.phnum = struct.unpack_from('<H', self.d, 44)[0]
        self.ph = [list(struct.unpack_from('<8I', self.d, self.phoff + 32 * i))
                   for i in range(self.phnum)]
        dyn = next(p for p in self.ph if p[0] == 2)
        self.dyn_off, self.dyn_size = dyn[1], dyn[4]
        self.tags = []
        for i in range(0, self.dyn_size, 8):
            tag, val = struct.unpack_from('<II', self.d, self.dyn_off + i)
            self.tags.append([tag, val])
            if tag == 0:
                break

    def off(self, va):
        for p in self.ph:
            if p[0] == 1 and p[2] <= va < p[2] + p[4]:
                return va - p[2] + p[1]
        raise ValueError(hex(va))

    def tag(self, name):
        return next(v for t, v in self.tags if t == DT[name])

    def set_tag(self, name, value):
        next(e for e in self.tags if e[0] == DT[name])[1] = value

    def u32(self, va):
        return struct.unpack_from('<I', self.d, self.off(va))[0]

    def put32(self, va, value):
        struct.pack_into('<I', self.d, self.off(va), value)

    def cstr(self, off):
        return bytes(self.d[off:self.d.index(b'\0', off)])


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    data = open(sys.argv[1], 'rb').read()
    if zlib.crc32(data) != FILE_CRC:
        sys.exit('%s: not TeknoRaven\'s Power Truck S pt (file CRC 0x%08x)' % (sys.argv[1], zlib.crc32(data)))
    imports = []
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'igs-pts-imports.txt')) as f:
        for line in f:
            if line.strip() and not line.startswith('#'):
                name, vfile, vname, flags = line.rstrip('\n').split('\t')
                imports.append((name, None if vfile == '-' else (vfile, vname), '*' in flags, 'w' in flags))
    img = Image(data)

    strtab, symtab = img.off(img.tag('STRTAB')), img.off(img.tag('SYMTAB'))
    strsz = img.tag('STRSZ')
    nsym = img.u32(img.tag('HASH') + 4)
    versym = img.off(img.tag('VERSYM'))

    # its versions' indexes by (file, name)
    versions = {}
    p = img.off(img.tag('VERNEED'))
    for _ in range(img.tag('VERNEEDNUM')):
        _, cnt, vfile, aux, nxt = struct.unpack_from('<HHIII', img.d, p)
        q = p + aux
        for _ in range(cnt):
            _, _, other, vname, qnext = struct.unpack_from('<IHHII', img.d, q)
            versions[(img.cstr(strtab + vfile).decode(), img.cstr(strtab + vname).decode())] = other
            q += qnext
        p += nxt

    # the PLT's stubs, in slot order: jmp *slot; push reloc; jmp plt0
    stubs = {}
    text = next(p for p in img.ph if p[0] == 1)
    for m in re.finditer(rb'\xff\x25(....)\x68....\xe9', bytes(img.d[text[1]:text[1] + text[4]]), re.S):
        slot = struct.unpack('<I', m.group(1))[0]
        if GOT_FIRST <= slot < GOT_FIRST + 4 * len(imports) and not (slot - GOT_FIRST) % 4:
            stubs[(slot - GOT_FIRST) // 4] = text[2] + m.start()
    if len(stubs) != len(imports):
        sys.exit('%d PLT stubs, %d imports listed' % (len(stubs), len(imports)))

    syms = [bytearray(img.d[symtab + 16 * i:symtab + 16 * i + 16]) for i in range(nsym)]
    vers = [struct.unpack_from('<H', img.d, versym + 2 * i)[0] for i in range(nsym)]
    strings = bytearray(img.d[strtab:strtab + strsz])
    index = {}
    for i in range(1, nsym):
        index.setdefault(img.cstr(strtab + struct.unpack_from('<I', syms[i], 0)[0]), i)
    # imports kept by TeknoRaven point into its table: undefined again
    for i in range(1, nsym):
        if struct.unpack_from('<H', syms[i], 14)[0] == 0:
            struct.pack_into('<I', syms[i], 4, 0)

    rel = bytearray()
    for n, (name, version, canon, weak) in enumerate(imports):
        key = name.encode()
        if key in index:
            i = index[key]
        else:
            i = len(syms)
            index[key] = i
            sym = bytearray(16)
            struct.pack_into('<IIIBBH', sym, 0, len(strings), 0, 0, 0x22 if weak else 0x12, 0, 0)
            strings += key + b'\0'
            syms.append(sym)
            vers.append(versions[version] if version else 1)
        stub = stubs[n]
        struct.pack_into('<I', syms[i], 4, stub if canon else 0)
        slot = GOT_FIRST + 4 * n
        rel += struct.pack('<II', slot, (i << 8) | 7)  # R_386_JMP_SLOT
        img.put32(slot, stub + 6)
        struct.pack_into('<I', img.d, img.off(stub) + 7, 8 * n)
    img.put32(GMON_SLOT, 0)

    nbucket = max(1, len(syms) // 2) | 1
    buckets, chains = [0] * nbucket, [0] * len(syms)
    for i in range(1, len(syms)):
        b = elf_hash(img_cstr(strings, struct.unpack_from('<I', syms[i], 0)[0])) % nbucket
        chains[i] = buckets[b]
        buckets[b] = i

    # the new segment, after the data segment's end
    last = max((p for p in img.ph if p[0] == 1), key=lambda p: p[2])
    file_off = (len(img.d) + 0xFFF) & ~0xFFF
    base = (last[2] + last[5] + 0xFFF) & ~0xFFF
    seg = bytearray()

    def place(blob, align=16):
        seg.extend(b'\0' * (-len(seg) % align))
        va = base + len(seg)
        seg.extend(blob)
        return va

    va_str = place(strings)
    va_sym = place(b''.join(syms))
    va_ver = place(b''.join(struct.pack('<H', v) for v in vers))
    va_hash = place(struct.pack('<II', nbucket, len(syms)) +
                    struct.pack('<%dI' % nbucket, *buckets) + struct.pack('<%dI' % len(chains), *chains))
    va_rel = place(rel)
    img.set_tag('STRTAB', va_str)
    img.set_tag('STRSZ', len(strings))
    img.set_tag('SYMTAB', va_sym)
    img.set_tag('VERSYM', va_ver)
    img.set_tag('HASH', va_hash)
    img.set_tag('JMPREL', va_rel)
    img.set_tag('PLTRELSZ', len(rel))
    for i, (tag, val) in enumerate(img.tags):
        struct.pack_into('<II', img.d, img.dyn_off + 8 * i, tag, val)

    # its program header after the last PT_LOAD (one slot is free before
    # PT_INTERP's string; PT_PHDR covers it)
    if img.phoff + 32 * (img.phnum + 1) > next(p[1] for p in img.ph if p[0] == 3):
        sys.exit('no room for another program header')
    at = img.ph.index(last) + 1
    img.ph.insert(at, [1, file_off, base, base, len(seg), len(seg), 4, 0x1000])
    # the segments' permissions as linked (TeknoRaven made them all RWE)
    loads = 0
    for p in img.ph:
        if p[0] == 1:
            p[6] = (5, 6, 4)[loads]
            loads += 1
        else:
            p[6] = {2: 6, 6: 5}.get(p[0], 4)
    for i, p in enumerate(img.ph):
        struct.pack_into('<8I', img.d, img.phoff + 32 * i, *p)
    struct.pack_into('<H', img.d, 44, len(img.ph))
    phdr = next(p for p in img.ph if p[0] == 6)
    if phdr[4] < 32 * len(img.ph):
        sys.exit('PT_PHDR too small')

    img.d.extend(b'\0' * (file_off - len(img.d)))
    img.d.extend(seg)
    with open(sys.argv[2], 'wb') as f:
        f.write(img.d)
    os.chmod(sys.argv[2], 0o755)
    print('%s: %d imports, %d symbols, file CRC 0x%08x' % (sys.argv[2], len(imports), len(syms), zlib.crc32(img.d)))


def img_cstr(strings, off):
    return bytes(strings[off:strings.index(b'\0', off)])


if __name__ == '__main__':
    main()
