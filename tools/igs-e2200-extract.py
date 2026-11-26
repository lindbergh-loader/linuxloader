#!/usr/bin/env python3
"""Build a game directory from an IGS E2200 disk (a MAME CHD of the whole
disk, e2200_*.chd, or its raw image): Soul of Gun, Fish Hunter SKL 2,
Full Attack, Ocean King, Poseidon, West Trip...

The disk (kernel 2.6.32 "James Kernel", igs_rofs v2 and IGS_SHFS):

  hda1  ext2, its metadata scrambled ("Game Image v2"), its data blocks plain:
        the kernels LILO boots
  hda2  the root, igs_rofs v2: IGS's AES, keys, constants and inode layout
        per kernel
  hda3  the game's data, IGS_SHFS (squashfs 4.0 behind an IGS superblock, as
        on the E3000), mounted on the game's directory (/SOG, /WT, ...)
  hda4  ext3, <game dir>/ScreenCutOff

The kernel builds the executable at boot (init_post), every game the same
way with its own names (kept as strings around "__KERNEL__" in the kernel):

  1. <postcat> (argv: <body> __KERNEL__ "-no-print -<game>") writes
     /mnt/GECA (a 1 KiB ELF header on raw hda2/3 + <body>: a loader) and
     /mnt/head (1 KiB of raw hda2/3, the executable's encrypted head);
  2. GECA cats head and the middles of <lib dir>/<prefix><c><ext>, for each
     character c of its order string, into <game dir>/<exe> (a ramfs);
  3. <postkick> <src> <dst> <key> has the FPGA (/dev/pccard0) decrypt it:
     the A36's cipher (igsA36.c) used the other way, encrypted =
     unpermute(plain) ^ mask, the mask being the encryption of a zero block
     (the file's most common block).

The root is extracted by running the kernel's own igs_rofs driver
(tools/igs-e2200/igs-rofs.c), the pieces' sectors are read from postcat's
code and GECA's parameters from its main's code (objdump).

The executables are still wrapped in a Sentinel HASP envelope (the cabinets
start aksusbd, winehasp and hasplmd first): their code needs the dongle.

Output: <out>/game (the game directory: data, the executable (named after
<dst>, which the game directory links to), lib/ with the
libraries it needs that a desktop lacks), <out>/boot (vmlinux, rootfs/: the
decrypted root). The cabinet runs ./<exe> from the game directory.

Needs chdman (for a CHD), unsquashfs, objdump, gcc with -m32. The work
directory needs the raw disk's size (about 4 GB) for a CHD.

E2100 disks (Scratch, Demon Hunter, Crazy Shoot, Hero of Robots, Ocean
Party...) differ: hda3 is plain squashfs, and init_post runs
"/bin/unrar <src> <dst> <key>", which streams <src> from the root through
the same FPGA cipher into a ramfs; the cabinet runs ./<NAME> (etc/X11/IGS)
from the game directory.

usage: igs-e2200-extract.py disk.chd|disk.img <out dir> [work dir]
"""

import collections
import importlib.util
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SECTOR = 512
SHFS_MAGIC = b'\x82\x26\xaa\xd4'

# libraries a desktop provides: not copied to game/lib
SYSTEM_LIBS = ('libc.so', 'libm.so', 'libdl.so', 'libpthread.so', 'librt.so', 'libGL.so', 'libX11.so',
               'libXext.so', 'libz.so', 'libstdc++.so', 'libgcc_s.so', 'libasound.so', 'libcrypt.so',
               'libresolv.so', 'libnsl.so', 'libutil.so', 'ld-linux.so',
               # (GLEW's: the desktop's, newer: the loader's glut needs XI2)
               'libXi.so')

# The FPGA cipher's bit permutation (igsA36.c cipherPerm): bit i of a 16-byte
# block, LSB first in each byte, goes to PERM[i].
PERM = [
    0x5f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e,
    0x1f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
    0x47, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76,
    0x77, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e,
    0x7d, 0x7e, 0x7f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x30, 0x31, 0x32, 0x33, 0x34,
    0x35, 0x36, 0x37, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x50, 0x51, 0x52, 0x53, 0x54,
    0x55, 0x56, 0x57, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x68, 0x69, 0x6a, 0x6b, 0x6c,
    0x6d, 0x6e, 0x6f, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x78, 0x79, 0x7a, 0x7b, 0x7c,
]


def partitions(f):
    f.seek(0)
    mbr = f.read(SECTOR)
    parts = []
    for i in range(4):
        e = mbr[0x1be + 16 * i:0x1be + 16 * (i + 1)]
        start, count = struct.unpack_from('<II', e, 8)
        parts.append((start * SECTOR, count * SECTOR))
    return parts


def copy_part(f, part, path):
    f.seek(part[0])
    left = part[1]
    with open(path, 'wb') as o:
        while left:
            b = f.read(min(left, 64 << 20))
            if not b:  # the partition table can claim more than the dump holds
                break
            o.write(b)
            left -= len(b)


def kernel(hda1):
    """The last bzImage on hda1, read through its ext2 block pointers (plain;
    the kernel file's indirect blocks sit between its data blocks)."""
    end = len(hda1)
    while True:  # the last one that starts a block (others are stray bytes)
        at = hda1.rfind(b'HdrS', 0, end) - 0x202
        if at < 0:
            sys.exit('no kernel on hda1')
        if at % 1024 == 0:
            break
        end = at + 0x202
    first = at // 1024
    syssize = struct.unpack_from('<I', hda1, at + 0x1f4)[0] * 16
    size = (hda1[at + 0x1f1] + 1) * SECTOR + syssize
    blk = lambda n: hda1[n * 1024:(n + 1) * 1024]
    ptrs = lambda n: [p for p in struct.unpack('<256I', blk(n)) if p]
    blocks = list(range(first, first + 12)) + ptrs(first + 12)
    for p in ptrs(blocks[-1] + 1):  # the double-indirect block follows
        blocks += ptrs(p)
    image = b''.join(blk(n) for n in blocks)[:size]
    payload = image.find(b'\x1f\x8b\x08', (image[0x1f1] + 1) * SECTOR)
    return zlib.decompressobj(31).decompress(image[payload:])  # stops at the stream's end


def kallsyms(v):
    """'address type name' lines from a 2.6.32 i386 vmlinux's kallsyms tables
    (addresses, num_syms, names, markers, token_table, token_index)."""
    for ti in range(0x800000, len(v) - 512, 2):  # token_index: 256 rising u16 from 0
        if v[ti:ti + 2] != b'\0\0':
            continue
        idx = struct.unpack_from('<256H', v, ti)
        if not all(idx[i] < idx[i + 1] for i in range(255)) or idx[255] > 3000:
            continue
        table = next((t for t in range(ti - idx[255] - 40, ti - idx[255])
                      if all(v[t + idx[i] - 1] == 0 for i in range(1, 256))), None)
        if table is not None:
            break
    else:
        sys.exit('no kallsyms')
    tokens = [v[table + idx[i]:v.index(b'\0', table + idx[i])] for i in range(256)]
    for off in range(0x800000, table, 4):
        n = struct.unpack_from('<I', v, off)[0]
        if not 1000 < n < 200000 or off - 4 * n < 0:
            continue
        first = struct.unpack_from('<3I', v, off - 4 * n)
        last = struct.unpack_from('<I', v, off - 4)[0]
        if not (all(0xc0000000 <= x < 0xc3000000 for x in first) and first[0] <= first[1] <= first[2] <= last < 0xc3000000):
            continue
        if not table - (off + 4) > n:  # the names, then the markers, sit between
            continue
        q = off + 4
        for _ in range(n):
            if not v[q]:  # every name has a type and a symbol
                break
            q += 1 + v[q]
            if q > table:
                break
        else:
            # then the markers, one u32 per 256 names, then the token table
            markers = ((q + 3) & ~3) + 4 * ((n + 255) // 256)
            if 0 <= table - markers < 16:
                break
    else:
        sys.exit('no kallsyms names')
    addrs = struct.unpack_from('<%dI' % n, v, off - 4 * n)
    lines, q = [], off + 4
    for i in range(n):
        s = b''.join(tokens[c] for c in v[q + 1:q + 1 + v[q]])
        q += 1 + v[q]
        lines.append('%08x %s %s' % (addrs[i], chr(s[0]), s[1:].decode('latin1')))
    return lines


def boot_names(v):
    """init_post's strings, in the same order in every E2200 kernel: <body>
    __KERNEL__ -no-print... NULL <postkick> <src> <dst> <key> ramfs /mnt
    tmpfs <postcat>."""
    i = v.index(b'\0__KERNEL__\0')
    body = v[v.rindex(b'\0', 0, i) + 1:i]
    rest = v[i + 1:i + 400].split(b'\0')
    names = [x.decode() for x in rest[:12]]
    if names[0] != '__KERNEL__' or names[2] != 'NULL' or names[7:9] != ['ramfs', '/mnt']:
        sys.exit('unexpected init_post strings: %s' % names)
    postcat = names[10] if names[9] == 'tmpfs' else names[9]  # Crazy Shoot (E2100) has no tmpfs
    return dict(body=body.decode(), argument=names[1], postkick=names[3], src=names[4], dst=names[5],
                key=names[6], postcat=postcat)


def e2100_names(v):
    """The E2100's init_post: /bin/unrar <src> <dst> <key> (unrar is no RAR
    tool: it streams <src> through the FPGA, keyed by <key>, into <dst> on
    a ramfs)."""
    i = v.find(b'\0/bin/unrar\0')
    if i < 0:
        return None
    names = [x.decode() for x in v[i + 1:i + 200].split(b'\0')[:4]]
    return dict(unrar=names[0], src=names[1], dst=names[2], key=names[3])


def igs_script(rootfs):
    try:
        return open(os.path.join(rootfs, 'etc/X11/IGS'), errors='replace').read()
    except OSError:
        return ''


def mount_point(rootfs):
    """Where the cabinet mounts hda3 (the game directory)."""
    m = re.search(r'^\s*mount\b.*/dev/hda3\s+(/\S+)', igs_script(rootfs), re.M)
    return m.group(1) if m else None


def run_name(rootfs, dst):
    """The executable the cabinet's script runs (./NAME in its loop), as a
    path in the game directory (its last cd into the mount point first)."""
    script, mnt = igs_script(rootfs), mount_point(rootfs)
    m = re.search(r'^\s*\./(\S+)', script, re.M)
    if not m:
        return os.path.basename(dst)
    cds = re.findall(r'^\s*cd\s+(\S+)', script[:m.start()], re.M)
    if mnt and cds and cds[-1].rstrip('/').startswith(mnt + '/'):
        return os.path.join(cds[-1].rstrip('/')[len(mnt) + 1:], m.group(1))
    return m.group(1)


def postcat_reads(path):
    """postcat's raw reads: (sector, '/dev/hdaN') for its 1 KiB copies."""
    d = open(path, 'rb').read()
    s = lambda va: d[va - 0x8048000:d.index(b'\0', va - 0x8048000)].decode()
    pattern = rb'\xc7\x44\x24\x0c\x00\x04\x00\x00\xc7\x44\x24\x08(....)\x89\x5c\x24\x04\xc7\x04\x24(....)'
    reads = [(struct.unpack('<I', m.group(1))[0], s(struct.unpack('<I', m.group(2))[0]))
             for m in re.finditer(pattern, d, re.S)]
    if len(reads) != 2:
        sys.exit('%s: expected two raw reads, found %s' % (path, reads))
    return reads


def geca_parameters(path):
    """The 14 arguments GECA's main hands its builder: tmp dir, -, game dir,
    exe name, lib dir, prefix, extension, order, skip mode, drop mode, skip,
    skip step, drop, drop step (modes: 1 add, 2 subtract)."""
    d = open(path, 'rb').read()
    ph = struct.unpack_from('<I', d, 28)[0]
    segs = []
    for i in range(struct.unpack_from('<H', d, 44)[0]):
        t, off, va, _, fs = struct.unpack_from('<5I', d, ph + 32 * i)
        if t == 1:
            segs.append((va, off, fs))

    def at(va, n=4):
        for sva, off, fs in segs:
            if sva <= va < sva + fs:
                return d[off + va - sva:off + va - sva + n]

    word = lambda va: struct.unpack('<I', at(va))[0]

    def string(va):
        for sva, off, fs in segs:
            if sva <= va < sva + fs:
                o = off + va - sva
                return d[o:d.index(b'\0', o)].decode()

    dis = subprocess.run(['objdump', '-d', '-M', 'suffix', path], capture_output=True, text=True, check=True).stdout
    lines = [l.split('\t')[-1].strip() for l in dis.splitlines() if '\t' in l]
    end = next(i for i, l in enumerate(lines) if l.startswith('rep movsl') and lines[i + 1].startswith('call'))
    start = max(i for i in range(end) if lines[i].startswith(('ret', 'jmp')))
    regs, slots, base = {}, {}, None
    for l in lines[start + 1:end]:
        m = re.match(r'movl? +\$0x([0-9a-f]+),(-0x[0-9a-f]+)\(%ebp\)$', l)
        if m:
            slots[int(m.group(2), 16)] = int(m.group(1), 16)
            continue
        m = re.match(r'movl? +0x([0-9a-f]+),%(e..)$', l)
        if m:
            regs[m.group(2)] = word(int(m.group(1), 16))
            continue
        m = re.match(r'movl? +\$0x([0-9a-f]+),%(e..)$', l)
        if m:
            regs[m.group(2)] = int(m.group(1), 16)
            continue
        m = re.match(r'movl? +%(e..),(-0x[0-9a-f]+)\(%ebp\)$', l)
        if m and m.group(1) in regs:
            slots[int(m.group(2), 16)] = regs[m.group(1)]
            continue
        m = re.match(r'leal? +(-0x[0-9a-f]+)\(%ebp\),%esi$', l)
        if m:
            base = int(m.group(1), 16)
    args = [slots.get(base + 4 * i) for i in range(14)]
    names = [string(a) if a else None for a in args[:8]]
    return dict(tmp=names[0], game=names[2], exe=names[3], libdir=names[4], prefix=names[5], ext=names[6],
                order=names[7], skip_mode=args[8], drop_mode=args[9], skip=args[10], skip_step=args[11],
                drop=args[12], drop_step=args[13])


def geca_build(root, head, p):
    """GECA's builder: head + the middle of each piece."""
    out = bytearray(head)
    skip, drop = p['skip'], p['drop']
    skip_mode = p['skip_mode'] if p['skip_step'] else 0
    drop_mode = p['drop_mode'] if p['drop_step'] else 0
    for i, c in enumerate(p['order']):
        path = os.path.join(root, p['libdir'].lstrip('/'), p['prefix'] + c + p['ext'])
        if not os.path.exists(path) or not os.path.getsize(path):
            continue
        with open(path, 'rb') as f:
            d = f.read()
        if i == len(p['order']) - 1:
            out += d[skip * 1024:skip * 1024 + len(d) - (drop + skip) * 1024]
        else:
            out += d[skip * 1024:skip * 1024 + (len(d) // 1024 - (drop + skip)) * 1024]
        skip += {1: p['skip_step'], 2: -p['skip_step']}.get(skip_mode, 0)
        drop += {1: p['drop_step'], 2: -p['drop_step']}.get(drop_mode, 0)
    return bytes(out)


def fpga_decrypt(data):
    """encrypted = unpermute(plain) ^ mask: plain = permute(encrypted ^ mask)."""
    blocks = collections.Counter(data[i:i + 16] for i in range(0, len(data) - 15, 16))
    mask = int.from_bytes(blocks.most_common(1)[0][0], 'little')
    table = [[sum(1 << PERM[pos * 8 + k] for k in range(8) if v >> k & 1) for v in range(256)] for pos in range(16)]
    out = bytearray(data)
    for at in range(0, len(data) - 15, 16):
        x = (int.from_bytes(data[at:at + 16], 'little') ^ mask).to_bytes(16, 'little')
        r = 0
        for pos in range(16):
            r |= table[pos][x[pos]]
        out[at:at + 16] = r.to_bytes(16, 'little')
    return bytes(out)


def needed(path):
    # readelf, not objdump -p: the envelope's section headers hide the
    # dynamic section from objdump
    d = subprocess.run(['readelf', '-dW', path], capture_output=True, text=True, env=dict(os.environ, LC_ALL='C')).stdout
    return re.findall(r'\(NEEDED\).*\[(.+?)\]', d)


def copy_libs(exe, rootfs, lib):
    """The libraries exe needs that a desktop lacks (and theirs), from the
    root to lib/."""
    todo, seen = list(needed(exe)), set()
    while todo:
        name = todo.pop(0)
        if name in seen:
            continue
        seen.add(name)
        if name.startswith(SYSTEM_LIBS):
            continue
        shipped = [n for n in sorted(os.listdir(lib)) if n == name or n.startswith(name + '.')] \
            if os.path.isdir(lib) else []
        if shipped:  # the game ships it (maybe without its soname link)
            if not os.path.lexists(os.path.join(lib, name)):
                os.symlink(shipped[-1], os.path.join(lib, name))
            todo += needed(os.path.realpath(os.path.join(lib, name)))
            continue
        src = find_lib(rootfs, name)
        if not src:
            print('%s: not on the root' % name)
            continue
        os.makedirs(lib, exist_ok=True)
        real = in_root(rootfs, src)
        shutil.copy2(real, os.path.join(lib, os.path.basename(real)))
        if os.path.basename(real) != name and not os.path.lexists(os.path.join(lib, name)):
            os.symlink(os.path.basename(real), os.path.join(lib, name))
        todo += needed(real)


def in_root(rootfs, path):
    """path with its links followed inside the root (absolute ones too)."""
    for _ in range(40):
        if not os.path.islink(path):
            return path
        t = os.readlink(path)
        path = os.path.join(rootfs, t.lstrip('/')) if t.startswith('/') else \
            os.path.normpath(os.path.join(os.path.dirname(path), t))
    return path


def find_lib(root, name):
    for sub in ('lib', 'usr/lib', 'usr/X11R6/lib', 'usr/local/lib'):
        p = os.path.join(root, sub, name)
        if os.path.lexists(p):
            return p


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    disk, out = sys.argv[1], sys.argv[2]
    work = sys.argv[3] if len(sys.argv) > 3 else tempfile.mkdtemp(prefix='igs-e2200-')
    os.makedirs(work, exist_ok=True)
    spec = importlib.util.spec_from_file_location('e3000', os.path.join(HERE, 'igs-e3000-extract.py'))
    e3000 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(e3000)

    raw = disk
    if disk.endswith('.chd'):
        raw = os.path.join(work, 'disk.raw')
        subprocess.run(['chdman', 'extracthd', '-f', '-i', disk, '-o', raw], check=True)
    game, boot = os.path.join(out, 'game'), os.path.join(out, 'boot')
    os.makedirs(boot, exist_ok=True)
    try:
        with open(raw, 'rb') as f:
            parts = partitions(f)
            f.seek(parts[0][0])
            vmlinux = kernel(f.read(parts[0][1]))
            f.seek(parts[2][0])
            sb = f.read(0xac)
            if sb[:4] not in (SHFS_MAGIC, b'hsqs'):  # E2100: plain squashfs
                sys.exit('hda3 is neither IGS_SHFS nor squashfs')
            copy_part(f, parts[1], os.path.join(work, 'hda2.img'))
            copy_part(f, parts[2], os.path.join(work, 'hda3.img'))
            if parts[3][1]:
                copy_part(f, parts[3], os.path.join(work, 'hda4.img'))
    finally:
        if raw != disk:
            os.remove(raw)

    vmpath = os.path.join(boot, 'vmlinux')
    with open(vmpath, 'wb') as o:
        o.write(vmlinux)
    e2100 = e2100_names(vmlinux)
    plain = b'\0__KERNEL__\0' not in vmlinux  # E2100: no GECA build at boot
    names = e2100 or ({} if plain else boot_names(vmlinux))
    symfile = os.path.join(work, 'kallsyms.txt')
    with open(symfile, 'w') as o:
        o.write('\n'.join(kallsyms(vmlinux)) + '\n')

    # the root, by the kernel's own driver
    helper = os.path.join(work, 'igs-rofs')
    subprocess.run(['gcc', '-m32', '-O1', '-fno-pie', '-no-pie', '-o', helper,
                    os.path.join(HERE, 'igs-e2200', 'igs-rofs.c')], check=True)
    rootfs = os.path.join(boot, 'rootfs')
    if os.path.exists(rootfs):
        shutil.rmtree(rootfs)
    subprocess.run([helper, vmpath, symfile, os.path.join(work, 'hda2.img'), rootfs], check=True)
    os.remove(helper)
    os.remove(symfile)

    if plain:
        os.remove(os.path.join(work, 'hda2.img'))
        shfs = os.path.join(work, 'hda3.img')
        if sb[:4] == SHFS_MAGIC:
            with open(shfs, 'r+b') as f:
                f.write(e3000.squashfs_superblock(sb))
        if os.path.exists(game):
            shutil.rmtree(game)
        subprocess.run(['unsquashfs', '-no-xattrs', '-q', '-f', '-d', game, shfs], check=True)
        os.remove(shfs)
        # the game directory links into a ramfs (/<Game>.bin) that the boot
        # fills from the root: plainly, or through unrar for <dst>
        mnt = mount_point(rootfs) or '/' + os.path.basename(game)
        # hda4 (ext3, the cabinet's writable data: Hero of Robots' videos and
        # music) where the script mounts it in the game directory
        m4 = re.search(r'^\s*mount\b.*/dev/hda4\s+(/\S+)', igs_script(rootfs), re.M)
        hda4 = os.path.join(work, 'hda4.img')
        if m4 and os.path.exists(hda4) and m4.group(1).startswith(mnt + '/'):
            dest = os.path.join(game, m4.group(1)[len(mnt) + 1:])
            os.makedirs(dest, exist_ok=True)
            subprocess.run(['debugfs', '-R', 'rdump / ' + dest, hda4], capture_output=True)
            shutil.rmtree(os.path.join(dest, 'lost+found'), ignore_errors=True)
        if os.path.exists(hda4):
            os.remove(hda4)
        links = [os.path.join(top, n) for top, dirs, files in os.walk(game) for n in dirs + files]
        for d in sorted(links):
            n = os.path.relpath(d, game)
            if not os.path.islink(d) or os.path.exists(d):
                continue
            target = os.path.normpath(os.path.join(mnt, os.path.dirname(n), os.readlink(d)))
            if target.startswith(mnt + '/'):
                continue  # inside the game directory (rw_data on hda4...)
            if e2100 and target == os.path.normpath(names['dst']):
                with open(os.path.join(rootfs, names['src'].lstrip('/')), 'rb') as f:
                    data = fpga_decrypt(f.read())
                if data[:4] != b'\x7fELF':
                    sys.exit('the executable did not decrypt')
            elif os.path.isfile(os.path.join(rootfs, target.lstrip('/'))):
                with open(os.path.join(rootfs, target.lstrip('/')), 'rb') as f:
                    data = f.read()
            else:
                print('%s -> %s: not on the root' % (n, target))
                continue
            os.remove(d)
            with open(d, 'wb') as f:
                f.write(data)
            os.chmod(d, 0o755)
        exe_name = run_name(rootfs, names['dst'] if e2100 else '')
        path = os.path.join(game, exe_name)
        with open(path, 'rb') as f:
            exe = f.read()
        if exe[:4] != b'\x7fELF':
            sys.exit('%s is not an ELF' % exe_name)
        copy_libs(path, rootfs, os.path.join(game, 'lib'))
        # and the libraries the game ships in its own library path's
        # directories (Hero of Robots' data/so)
        m = re.search(r'LD_LIBRARY_PATH=(\S+)', igs_script(rootfs))
        for d in (m.group(1).split(':') if m else []):
            if mnt and d.rstrip('/').startswith(mnt + '/'):
                sub = os.path.join(game, d.rstrip('/')[len(mnt) + 1:])
                for n in sorted(os.listdir(sub)) if os.path.isdir(sub) else []:
                    if '.so' in n and os.path.isfile(os.path.join(sub, n)):
                        copy_libs(os.path.join(sub, n), rootfs, os.path.join(game, 'lib'))
        print('%s: %d bytes%s; run from game/ as ./%s' % (
            path, len(exe), ', from %s (key %s)' % (names['src'], names['key']) if e2100 else '', exe_name))
        return

    # postcat's two pieces: GECA's ELF header and the executable's head
    pieces = []
    for sector, dev in postcat_reads(os.path.join(rootfs, names['postcat'].lstrip('/'))):
        with open(os.path.join(work, 'hda%s.img' % dev[-1]), 'rb') as f:
            f.seek(sector * SECTOR)
            pieces.append(f.read(1024))
    os.remove(os.path.join(work, 'hda2.img'))
    geca_head = next(p for p in pieces if p[:4] == b'\x7fELF')
    head = next(p for p in pieces if p[:4] != b'\x7fELF')
    geca = os.path.join(work, 'GECA')
    with open(geca, 'wb') as o, open(os.path.join(rootfs, names['body'].lstrip('/')), 'rb') as b:
        o.write(geca_head + b.read())
    params = geca_parameters(geca)
    os.remove(geca)

    # the data: hda3 as plain squashfs
    shfs = os.path.join(work, 'hda3.img')
    if sb[:4] == SHFS_MAGIC:
        with open(shfs, 'r+b') as f:
            f.write(e3000.squashfs_superblock(sb))
    if os.path.exists(game):
        shutil.rmtree(game)
    subprocess.run(['unsquashfs', '-no-xattrs', '-q', '-f', '-d', game, shfs], check=True)
    os.remove(shfs)

    exe = fpga_decrypt(geca_build(rootfs, head, params))
    if exe[:4] != b'\x7fELF':
        sys.exit('the executable did not decrypt')
    exe_name = os.path.basename(names['dst'])  # postkick's output, which the game directory links to
    path = os.path.join(game, exe_name)
    if os.path.lexists(path):
        os.remove(path)  # the cabinet's link to <dst>
    with open(path, 'wb') as f:
        f.write(exe)
    os.chmod(path, 0o755)

    copy_libs(path, rootfs, os.path.join(game, 'lib'))
    print('%s: %d bytes, from %s (key %s); run from game/ as ./%s' % (
        path, len(exe), names['src'], names['key'], exe_name))


if __name__ == '__main__':
    main()
