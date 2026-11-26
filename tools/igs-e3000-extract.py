#!/usr/bin/env python3
"""Build a game directory from an IGS E3000 disk (Speed Driver 4's
e3000_speeddriver4.chd, a MAME CHD of the whole disk, or its raw image).

The disk (kernel 2.6.32.8 "James Kernel", igs_rofs and IGS_SHFS):

  hda1  ext2 whose metadata is scrambled ("Game Image v2"); its data blocks
        are plain: the kernels (LILO), not needed here
  hda2  the root, igs_rofs v2 (encrypted), not needed here
  hda3  the game's data, IGS_SHFS: squashfs 4.0 (LZO) behind a 0xac-byte
        IGS superblock, mounted on /SD4 (data/)
  hda4  ext3, the game's /SD4/readwrite: the executable, its libraries,
        settings, logs

The IGS superblock: the magic d4aa2682, the IGS banner to 0x40, then the
squashfs fields in another order (0x54..0x63, IGS's version words, are left
out):

  0x44 root inode     0x4c bytes used    0x64 inodes      0x68 mkfs time
  0x6c block size     0x70 fragments     0x74 compressor  0x76 block log
  0x78 flags          0x7a ids           0x7c id table    0x84 xattr table
  0x8c inode table    0x94 dir table     0x9c frag table  0xa4 lookup table

A standard superblock (96 bytes) is written over the IGS one in the work
copy of the disk (the data starts at 0xac, past both) and unsquashfs reads
the partition in place (-o). hda4 is copied out with debugfs. go_product
names the executable (olympus: UPX-packed, unpacked here with upx -d, the
packed file kept as <name>.upx).

Output: <out>/data (hda3's data/) and <out>/game (hda4), as the cabinet's
/SD4/data and /SD4/readwrite (<out>/readwrite links to game): the game
runs from game/ with ../data.

Needs chdman (for a CHD), unsquashfs with LZO, debugfs, upx. The work
directory needs the raw disk's size (8 GB for Speed Driver 4); a raw image
given as input is copied there first, the input is never written.

usage: igs-e3000-extract.py disk.chd|disk.img "games/Speed Driver 4" [work dir]
"""

import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

SECTOR = 512
SHFS_MAGIC = b'\x82\x26\xaa\xd4'


def partitions(f):
    f.seek(0)
    mbr = f.read(SECTOR)
    parts = []
    for i in range(4):
        e = mbr[0x1be + 16 * i:0x1be + 16 * (i + 1)]
        start, count = struct.unpack_from('<II', e, 8)
        if e[4]:
            parts.append((start * SECTOR, count * SECTOR))
    return parts


def squashfs_superblock(h):
    u16 = lambda o: struct.unpack_from('<H', h, o)[0]
    u32 = lambda o: struct.unpack_from('<I', h, o)[0]
    u64 = lambda o: struct.unpack_from('<Q', h, o)[0]
    return struct.pack('<IIIIIHHHHHHQQQQQQQQ', 0x73717368, u32(0x64), u32(0x68), u32(0x6c), u32(0x70),
                       u16(0x74), u16(0x76), u16(0x78), u16(0x7a), 4, 0,
                       u64(0x44), u64(0x4c), u64(0x7c), u64(0x84), u64(0x8c), u64(0x94), u64(0x9c), u64(0xa4))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    disk, out = sys.argv[1], sys.argv[2]
    work = sys.argv[3] if len(sys.argv) > 3 else tempfile.mkdtemp(prefix='igs-e3000-')
    os.makedirs(work, exist_ok=True)
    raw = os.path.join(work, 'disk.raw')
    if disk.endswith('.chd'):
        subprocess.run(['chdman', 'extracthd', '-f', '-i', disk, '-o', raw], check=True)
    else:
        shutil.copyfile(disk, raw)
    try:
        with open(raw, 'r+b') as f:
            parts = partitions(f)
            data = rw = None
            for at, size in parts:
                f.seek(at)
                head = f.read(0x800)
                if head[:4] == SHFS_MAGIC and b'Game Image' in head[:0x40]:
                    data = (at, head[:0xac])
                elif struct.unpack_from('<H', head, 0x438)[0] == 0xef53:
                    rw = (at, size)
            if not data or not rw:
                sys.exit('no IGS_SHFS or ext3 partition: %s' % parts)
            f.seek(data[0])
            f.write(squashfs_superblock(data[1]))

        os.makedirs(out, exist_ok=True)
        fs = os.path.join(work, 'shfs')
        subprocess.run(['unsquashfs', '-no-xattrs', '-q', '-f', '-o', str(data[0]), '-d', fs, raw], check=True)
        if os.path.exists(os.path.join(out, 'data')):
            shutil.rmtree(os.path.join(out, 'data'))
        shutil.move(os.path.join(fs, 'data'), os.path.join(out, 'data'))
        shutil.rmtree(fs)

        part = os.path.join(work, 'readwrite.img')
        with open(raw, 'rb') as f, open(part, 'wb') as p:
            f.seek(rw[0])
            left = rw[1]
            while left:
                b = f.read(min(left, 64 << 20))
                p.write(b)
                left -= len(b)
    finally:
        os.remove(raw)

    game = os.path.join(out, 'game')
    os.makedirs(game, exist_ok=True)
    # rdump keeps the files; ownership cannot be set without root
    subprocess.run(['debugfs', '-R', 'rdump / %s' % game, part], stderr=subprocess.DEVNULL)
    os.remove(part)
    shutil.rmtree(os.path.join(game, 'lost+found'), ignore_errors=True)

    # The cabinet's /SD4/readwrite: some data names it (the force feedback's
    # effects, ../readwrite/physics/ffb/...).
    link = os.path.join(out, 'readwrite')
    if not os.path.lexists(link):
        os.symlink('game', link)

    product = os.path.join(game, 'go_product')
    if os.path.exists(product):
        with open(product) as f:
            m = re.search(r'\./(\S+)\s*(.*)', f.read())
        if m:
            exe, arguments = m.group(1), m.group(2).strip()
            path = os.path.join(game, exe)
            with open(path, 'rb') as f:
                packed = b'UPX!' in f.read(0x1000)
            if packed:
                shutil.copy2(path, path + '.upx')
                subprocess.run(['upx', '-q', '-d', path], check=True, stdout=subprocess.DEVNULL)
            print('%s: the game, run from game/ as ./%s %s%s' % (path, exe, arguments,
                                                                 ' (UPX unpacked)' if packed else ''))


if __name__ == '__main__':
    main()
