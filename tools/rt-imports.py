#!/usr/bin/env python3
"""Game import table of a 32-bit Raw Thrills envelope dump (RtGameImport).

The envelope leaves its decrypted import list in the dump: NUL-separated
names, each followed by its version when it has one (GL and X11 imports have
none). Its order is the game GOT's, skipping the slots the executable's own
relocations cover (DT_JMPREL). This prints the C table for rtGames.c.

usage: rt-imports.py <dump> <first GOT slot> <first import name> <array name>
  e.g. rt-imports.py "Doodle Jump/game" 0x822148c waitpid djGameImports
"""

import re
import struct
import sys

VERSION = re.compile(r"^(GLIBC|GLIBCXX|CXXABI|GCC)_")


def segments(elf):
    phoff, = struct.unpack_from("<I", elf, 0x1c)
    phentsize, phnum = struct.unpack_from("<HH", elf, 0x2a)
    for i in range(phnum):
        ptype, off, vaddr, _, filesz, _, _, _ = struct.unpack_from("<8I", elf, phoff + i * phentsize)
        yield ptype, off, vaddr, filesz


def offset(elf, va):
    for ptype, off, vaddr, filesz in segments(elf):
        if ptype == 1 and vaddr <= va < vaddr + filesz:
            return off + va - vaddr
    raise ValueError(hex(va))


def relocated_slots(elf):
    dyn = next(off for ptype, off, _, _ in segments(elf) if ptype == 2)
    tags = {}
    for i in range(0, 4096, 8):
        tag, val = struct.unpack_from("<iI", elf, dyn + i)
        if tag == 0:
            break
        tags[tag] = val
    slots = set()
    for table, size in ((23, 2), (17, 18)):  # DT_JMPREL/DT_PLTRELSZ, DT_REL/DT_RELSZ
        if table in tags:
            base = offset(elf, tags[table])
            for i in range(0, tags[size], 8):
                slots.add(struct.unpack_from("<I", elf, base + i)[0])
    return slots


def import_list(elf, first):
    # The name is in the dynamic string table too: the longest list wins.
    best, needle = [], b"\0" + first.encode() + b"\0"
    pos = elf.find(needle)
    while pos >= 0:
        entries = parse_list(elf, pos + 1)
        if len(entries) > len(best):
            best = entries
        pos = elf.find(needle, pos + 1)
    return best


def parse_list(elf, start):
    entries = []
    pos = start
    while True:
        end = elf.index(b"\0", pos)
        token = elf[pos:end]
        if not token or not all(0x20 <= c < 0x7f for c in token):
            break
        token = token.decode()
        if VERSION.match(token) and entries:
            entries[-1][1] = token
        else:
            entries.append([token, ""])
        pos = end + 1
    return entries


def main():
    path, got, first, array = sys.argv[1], int(sys.argv[2], 0), sys.argv[3], sys.argv[4]
    elf = open(path, "rb").read()
    skip = relocated_slots(elf)
    slot = got
    print("static const RtGameImport %s[] = {" % array)
    for name, version in import_list(elf, first):
        while slot in skip:
            slot += 4
        print('    {0x%x, "%s", "%s"},' % (slot, name, version))
        slot += 4
    print("};")


if __name__ == "__main__":
    main()
