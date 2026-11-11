#!/usr/bin/env python3
"""Build a game directory from an IGS (International Games System) PC disk
image: Gold Captain's (GoldCaptain.img, IGS-Linux on kernel 2.4.31-IGS) or
Pirate Revenge's (Pirate Revenge (M-101CN).img, IGS-Linux of 2008).

The disk: hda1 ext2 (the kernel), hda2 the root (IGS "rofs": cramfs with its
inode words reordered), hda4 ext3 (the game's var/ or /fec_data), hda3 the
game's data (IGS "SHFS": squashfs 2.x with its superblock reworked).

Gold Captain: hda3 is squashfs 2.0 behind a longer superblock (the IGS
signature first). The kernel mounts hda3 on /GoldCaptain, hda4 on
/GoldCaptain/var, and runs /bin/csh, a loader that rebuilds the game's ELF in
a ramfs: a 1 KiB head kept past the root filesystem's end on hda2, then the
start of /lib/libdbN.a in the order its table gives, each cut shorter by 17
KiB than the one before.

Pirate Revenge: hda3 is squashfs 2.1, its superblock's words in another
order (the major and minor versions 32-bit), its directories unsorted (which
today's unsquashfs refuses: it is read here). The kernel mounts it on
/pr/linux, where /etc/X11/IGS runs the game's ELF, sship, as it is.

The ELF names /lib/libcasmcard.so by its absolute path: it is renamed to its
soname here (the file CRC the loader knows is that of the renamed ELF), the
library and libpcsclite.so.0 go in the game's lib/.

Needs unsquashfs (squashfs-tools 4.x reads squashfs 2.0) for Gold Captain.

usage: igs-extract.py GoldCaptain.img "games/Gold Captain" [work dir]
       igs-extract.py "Pirate Revenge (M-101CN).img" "games/Pirate Revenge" [work dir]
"""

import mmap
import os
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import zlib

SECTOR = 512


def partitions(img):
    with open(img, 'rb') as f:
        mbr = f.read(SECTOR)
    parts = []
    for i in range(4):
        e = mbr[0x1be + 16 * i:0x1be + 16 * (i + 1)]
        kind, start, count = e[4], struct.unpack_from('<I', e, 8)[0], struct.unpack_from('<I', e, 12)[0]
        if kind:
            parts.append((start * SECTOR, count * SECTOR))
    return parts


def read(img, offset, size):
    with open(img, 'rb') as f:
        f.seek(offset)
        return f.read(size)


# --- rofs: cramfs, an inode's words [mtime][namelen|offset<<6][size][mode|uid<<16][name]

def rofs_extract(data, out):
    def inode(o):
        _, w1, size, mu = struct.unpack_from('<IIII', data, o)
        nl = (w1 & 0x3f) * 4
        return {'mode': mu & 0xffff, 'size': size & 0xffffff, 'off': (w1 >> 6) * 4,
                'name': data[o + 16:o + 16 + nl].rstrip(b'\0').decode('latin1'), 'len': 16 + nl}

    def walk(ino, path):
        m = ino['mode']
        if stat.S_ISDIR(m):
            os.makedirs(path, exist_ok=True)
            o, end = ino['off'], ino['off'] + ino['size']
            while o < end:
                c = inode(o)
                o += c['len']
                walk(c, os.path.join(path, c['name']))
        elif stat.S_ISREG(m) or stat.S_ISLNK(m):
            n = (ino['size'] + 4095) // 4096
            ptrs = struct.unpack_from('<%dI' % n, data, ino['off'])
            s, body = ino['off'] + 4 * n, b''
            for p in ptrs:
                body += zlib.decompress(data[s:p]) if p > s else b'\0' * 4096
                s = p
            body = body[:ino['size']]
            if stat.S_ISLNK(m):
                if os.path.lexists(path):
                    os.remove(path)
                os.symlink(body.decode('latin1'), path)
            else:
                with open(path, 'wb') as f:
                    f.write(body)
                os.chmod(path, m & 0o777 | 0o200)

    walk(inode(0xa8), out)


# --- SHFS: squashfs 2.0, its superblock 0x8b bytes from the IGS magic on

def shfs_to_squashfs(data):
    inodes, used, uid, gid, itable, dtable = struct.unpack_from('<6I', data, 0x58)
    flags, nuid, ngid = data[0x70], data[0x71], data[0x72]
    mkfs, = struct.unpack_from('<I', data, 0x73)
    root, = struct.unpack_from('<Q', data, 0x77)
    bsize, frags, ftable = struct.unpack_from('<3I', data, 0x7f)
    sb = struct.pack('<7I', 0x73717368, inodes, used, uid, gid, itable, dtable)
    sb += struct.pack('<4H', 2, 0, 0 if bsize > 0xffff else bsize, bsize.bit_length() - 1)
    sb += bytes([flags, nuid, ngid]) + struct.pack('<IQ3I', mkfs, root, bsize, frags, ftable)
    return sb.ljust(0x8b, b'\0') + data[0x8b:used]


# --- SHFS 2.1 (Pirate Revenge): squashfs 2.1, its superblock's fields
# [magic][block_size_1 | block_log << 16][major][minor][inodes][bytes_used]
# [uid_start][guid_start][inode_table][directory_table][flags][no_uids]
# [no_guids][mkfs_time][root_inode, 8][block_size][fragments]
# [fragment_table], 0x43 bytes, its data next

SHFS21_MAGIC = b'\x82\x26\xaa\xd4'


def shfs21_extract(img, at, out):
    """Squashfs 2.x read as the IGS mkfs wrote it, its directories unsorted."""
    with open(img, 'rb') as f:
        d = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    itable, dtable = struct.unpack_from('<2I', d, at + 0x20)
    root, = struct.unpack_from('<Q', d, at + 0x2f)
    bsize, frags, ftable = struct.unpack_from('<3I', d, at + 0x37)
    used, = struct.unpack_from('<I', d, at + 0x14)

    def metadata(start, end):
        # Its 8 KiB blocks, each behind a 16-bit header (bit 15: stored), and
        # where each block (by its offset in the table) starts once inflated.
        buf, o, starts = bytearray(), start, {}
        while o < end:
            h, = struct.unpack_from('<H', d, at + o)
            starts[o - start] = len(buf)
            block = d[at + o + 2:at + o + 2 + (h & 0x7fff)]
            buf += block if h & 0x8000 else zlib.decompress(block)
            o += 2 + (h & 0x7fff)
        return bytes(buf), starts

    inodes, inode_at = metadata(itable, dtable)
    dirs, dir_at = metadata(dtable, ftable if frags else used)
    fragments = b''
    for p in struct.unpack_from('<%dI' % ((frags * 8 + 8191) // 8192), d, at + ftable) if frags else ():
        h, = struct.unpack_from('<H', d, at + p)
        fragments += metadata(p, p + 2 + (h & 0x7fff))[0]

    def block(start, size):
        # Bit 24: stored.
        raw = d[at + start:at + start + (size & ~(1 << 24))]
        return raw if size & (1 << 24) else zlib.decompress(raw)

    def walk(ref, path):
        o = inode_at[ref >> 16] + (ref & 0xffff)
        w, = struct.unpack_from('<I', inodes, o)
        kind, mode = w & 0xf, (w >> 4) & 0xfff
        if kind in (1, 8):  # directory, long directory
            if kind == 1:
                v, = struct.unpack_from('<I', inodes, o + 4)
                size, offset, start = v & 0x7ffff, v >> 19, int.from_bytes(inodes[o + 12:o + 15], 'little')
            else:
                v, = struct.unpack_from('<Q', inodes, o + 4)
                size, offset = v & 0x7ffffff, (v >> 27) & 0x1fff
                start = int.from_bytes(inodes[o + 14:o + 17], 'little')
            os.makedirs(path, exist_ok=True)
            p = dir_at[start] + offset
            end = p + size
            while p < end - 3:
                h, = struct.unpack_from('<I', dirs, p)
                count, inode_block = (h & 0xff) + 1, h >> 8
                p += 4
                for _ in range(count):
                    e, = struct.unpack_from('<I', dirs, p)
                    n = ((e >> 16) & 0xff) + 1
                    name = dirs[p + 3:p + 3 + n].decode('latin1')
                    p += 3 + n
                    walk(inode_block << 16 | (e & 0x1fff), os.path.join(path, name))
            os.chmod(path, mode | 0o700)
        elif kind == 2:  # file
            _, start, fragment, frag_offset, size = struct.unpack_from('<iIIII', inodes, o + 4)
            n = (size + bsize - 1) // bsize if fragment == 0xffffffff else size // bsize
            with open(path, 'wb') as f:
                for s in struct.unpack_from('<%dI' % n, inodes, o + 24):
                    f.write(block(start, s) if s else b'\0' * bsize)
                    start += s & ~(1 << 24)
                if fragment != 0xffffffff:
                    fs, fsize = struct.unpack_from('<II', fragments, fragment * 8)
                    f.write(block(fs, fsize)[frag_offset:frag_offset + size % bsize])
                f.truncate(size)
            os.chmod(path, mode | 0o200)
        elif kind == 3:  # symbolic link
            n, = struct.unpack_from('<H', inodes, o + 4)
            if os.path.lexists(path):
                os.remove(path)
            os.symlink(inodes[o + 6:o + 6 + n].decode('latin1'), path)

    walk(root, out)


# --- the game's ELF, as /bin/csh rebuilds it

def rebuild_elf(root_part, rootfs):
    # The head: the 1 KiB ELF header past the root filesystem's data.
    end = len(root_part.rstrip(b'\0'))
    head_at = root_part.rfind(b'\x7fELF', 0, end)
    elf = bytearray(root_part[head_at:head_at + 0x400])
    trim, order = 0x3ec, '47632518'
    for i, c in enumerate(order):
        with open(os.path.join(rootfs, 'lib', 'libdb%s.a' % c), 'rb') as f:
            piece = f.read()
        n = ((len(piece) >> 10) - trim) << 10 if i < len(order) - 1 else len(piece) - (trim << 10)
        elf += piece[:n]
        trim -= 17
    return elf


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    img, out = sys.argv[1], sys.argv[2]
    work = sys.argv[3] if len(sys.argv) > 3 else tempfile.mkdtemp(prefix='igs-')
    (_, _), (root_at, root_size), (data_at, data_size), _ = partitions(img)

    root_part = read(img, root_at, root_size)
    rootfs = os.path.join(work, 'rootfs')
    rofs_extract(root_part, rootfs)

    game = os.path.join(work, 'game')
    if read(img, data_at, 4) == SHFS21_MAGIC:
        shfs21_extract(img, data_at, game)
    else:
        sqsh = os.path.join(work, 'game.sqsh')
        with open(sqsh, 'wb') as f:
            f.write(shfs_to_squashfs(read(img, data_at, data_size)))
        subprocess.run(['unsquashfs', '-no-xattrs', '-f', '-d', game, sqsh], check=True, stdout=subprocess.DEVNULL)

    os.makedirs(out, exist_ok=True)
    subprocess.run(['cp', '-a', game + '/.', out], check=True)
    if os.path.exists(os.path.join(rootfs, 'lib', 'libdb4.a')):
        elf = rebuild_elf(root_part, rootfs)
        name = os.readlink(os.path.join(game, 'GoldCaptain')).rsplit('/', 1)[1] \
            if os.path.islink(os.path.join(game, 'GoldCaptain')) else 'GoldCaptain'
    else:
        name = 'sship'
        with open(os.path.join(game, name), 'rb') as f:
            elf = bytearray(f.read())
    absolute = b'/lib/libcasmcard.so\0'
    at = elf.find(absolute)
    if at >= 0:
        elf[at:at + len(absolute)] = b'libcasmcard.so'.ljust(len(absolute), b'\0')
    target = os.path.join(out, name)
    if os.path.lexists(target):
        os.remove(target)
    with open(target, 'wb') as f:
        f.write(elf)
    os.chmod(target, 0o755)
    print('%s: %d bytes, CRC32 %08x' % (target, len(elf), zlib.crc32(elf)))

    lib = os.path.join(out, 'lib')
    os.makedirs(lib, exist_ok=True)
    cardread = os.path.join(rootfs, 'usr', 'sbin', 'cardread', 'lib')
    for so in ('libcasmcard.so', 'libpcsclite.so.0'):
        shutil.copy(os.path.realpath(os.path.join(cardread, so)), os.path.join(lib, so))
    os.makedirs(os.path.join(out, 'var', 'temp'), exist_ok=True)


if __name__ == '__main__':
    main()
