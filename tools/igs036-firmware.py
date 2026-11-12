#!/usr/bin/env python3
"""Decrypt the IGS036 (A36) firmware an IGS E3000 game uploads to its board:
Speed Driver 4's olympus embeds it, Speed Driver 3 and Speed Rider 2 load it
from a file (P2A_LoaderAdv_iLoadData, "../sd3.bin" / "sr2.bin").

The image is a 64 KiB header (the game decrypts it with the FPGA and reads
the CRC offsets from it) then the ROM, 512 KiB. The game sends the ROM as it
is; on its way to the IGS036 it goes through the FPGA's cipher and the
IGS036 decrypts it with its own scheme:

  rom = igs036_decrypt(fpga(body))

- fpga: per 16-byte block, bit i moves to bit perm[i] (the "encrypt" mapping
  of igsA36.c's cipher()), then the block is XORed with a 16-byte mask.
- igs036_decrypt: MAME's igs036crypt.cpp (Andreas Naive, David Haywood): an
  address-driven rotation and bitswap of each 16-bit word, then an XOR of key
  bits chosen by address triggers from a 256-byte game key.

Neither the FPGA mask nor the game key is known up front. Everything is
linear over GF(2), and the ROM's unused tail is a 256-byte pattern repeated
up to the end (with a 20-byte magic and hash hidden in it): the repeated
ciphertext blocks give a linear system in the pattern, the mask and the key,
solved here. Key bits the fill never reaches are then voted from known
plaintext: code and strings shared with other decrypted IGS036 ROMs (--ref,
found at any offset), then a byte-bigram model trained on them. The fill,
now known, re-votes every bit it reaches, and the result is checked with the
IGS036's own hash (MAME's calculate_hash; the two shifts are searched).

usage: igs036-firmware.py [--elf olympus --at 0x09020600 --size 0x90000 |
                           --image sd3.bin] [--ref rom.bin ...] out.bin

--at and --size are found from Real36::loadBinTo36 when the ELF has its
symbols (olympus does). Prints the FPGA mask and the key, and exits 1 when
the hash does not match.
"""

import argparse
import collections
import math
import re
import struct
import subprocess
import sys

HEADER = 0x10000

# --- the FPGA's cipher (igsA36.c cipherPerm) ---

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


def fpga_perm(data):
    out = bytearray()
    for at in range(0, len(data) - 15, 16):
        x, y = int.from_bytes(data[at:at + 16], 'little'), 0
        for i in range(128):
            if x >> i & 1:
                y |= 1 << PERM[i]
        out += y.to_bytes(16, 'little')
    return bytes(out)


# --- MAME's igs036crypt.cpp, BSD-3-Clause (Andreas Naive, David Haywood) ---

def BIT(x, n):
    return (x >> n) & 1


def _zero(a): return 0
def _one(a): return 1
def _b3(a): return BIT(a, 3)
def _b4(a): return BIT(a, 4)
def _b7(a): return BIT(a, 7)
def _n3(a): return BIT(a, 3) ^ 1
def _n4(a): return BIT(a, 4) ^ 1
def _n7(a): return BIT(a, 7) ^ 1
def _x37(a): return BIT(a, 3) ^ BIT(a, 7)
def _xn37(a): return BIT(a, 3) ^ BIT(a, 7) ^ 1
def _x47(a): return BIT(a, 4) ^ BIT(a, 7)
def _xn47(a): return BIT(a, 4) ^ BIT(a, 7) ^ 1
def _nor34(a): return (BIT(a, 3) | BIT(a, 4)) ^ 1
def _impl43(a): return int(bool(BIT(a, 3) or (BIT(a, 4) ^ 1)))


ROT_ENABLING = [
    [_b3, _n3, _b3, _n3], [_b3, _n3, _b3, _n3], [_b4, _b4, _b4, _b4], [_b4, _n4, _b4, _n4],
    [_b3, _b3, _b3, _b3], [_nor34, _b7, _b7, _zero], [_zero, _one, _zero, _one],
    [_impl43, _x37, _xn37, _n3], [_b3, _b3, _n3, _n3], [_b4, _b4, _n4, _n4],
    [_zero, _zero, _zero, _zero], [_nor34, _b7, _n7, _one], [_b3, _n3, _b3, _n3],
    [_zero, _one, _one, _zero], [_b4, _n4, _b4, _n4], [_zero, _zero, _zero, _zero],
]
ROT_DIRECTION = [
    [_b3, _x37, _xn37, _n3, _b3, _x37, _xn37, _n3],
    [_zero, _n7, _n7, _zero, _zero, _n7, _n7, _zero],
    [_b4, _x47, _xn47, _n4, _b4, _x47, _xn47, _n4],
    [_b3, _n7, _b7, _zero, _one, _n7, _b7, _zero],
]
TRIGGERS = [
    (0x000101, 0x000001), (0x000802, 0x000800), (0x000204, 0x000004), (0x000408, 0x000408),
    (0x010010, 0x000010), (0x020020, 0x000020), (0x040040, 0x000040), (0x080080, 0x080080),
    (0x100100, 0x000100), (0x200200, 0x200000), (0x400400, 0x400000), (0x800801, 0x000001),
    (0x001004, 0x001000), (0x002010, 0x002000), (0x004040, 0x000040), (0x008100, 0x008100),
]
FIXED = 0x1a3a  # the XOR of a bit whose trigger is off
SWAP = [10, 9, 8, 7, 0, 15, 6, 5, 14, 13, 4, 3, 12, 11, 2, 1]
GROUPS = ([15, 11, 7, 5], [14, 9, 3, 2], [13, 10, 6, 1], [12, 8, 4, 0])


def _rot_enabled(a, group):
    for g in group:
        if BIT(a, 8 + g):
            aux = a ^ (0x1b * BIT(a, 2))
            return ROT_ENABLING[g][aux & 3](aux)
    return 0


def _rot_group(a, group):
    return ROT_DIRECTION[group[0] & 3][a & 7](a) * 2 - 1


def rotation(a):
    e0 = _rot_enabled(a, GROUPS[0])
    rot = e0 * _rot_group(a, GROUPS[0]) * 9
    for group, step in zip(GROUPS[1:], (1, 2, 4)):
        rot += (e0 ^ _rot_enabled(a, group)) * _rot_group(a, group) * step
    r2 = 4 * BIT(a, 0)
    r2 += 1 * BIT(a, 4) * (BIT(a, 0) * 2 - 1)
    r2 += 4 * BIT(a, 3) * (BIT(a, 0) * 2 - 1)
    r2 *= (BIT(a, 7) | (BIT(a, 0) ^ BIT(a, 1) ^ 1)) * 2 - 1
    r2 += 2 * ((BIT(a, 0) ^ BIT(a, 1)) & (BIT(a, 7) ^ 1))
    return (rot + r2) & 0xf


def deobfuscate(w, rot):
    w = ((w << rot) | (w >> (16 - rot))) & 0xffff
    out = 0
    for k, b in enumerate(SWAP):
        out |= BIT(w, b) << (15 - k)
    return out


class Rom:
    """The ROM's words before the key: deobfuscate(fpga_perm(body)) without
    the FPGA mask, which deobfuscates as a per-address constant."""

    def __init__(self, permuted):
        self.words = struct.unpack('<%dH' % (len(permuted) // 2), permuted)
        self.n = len(self.words)
        self.rot = [rotation(a) for a in range(self.n)]
        self.active = [[(a & t0) == t1 for t0, t1 in TRIGGERS] for a in range(self.n)]
        self.z = [deobfuscate(w, r) for w, r in zip(self.words, self.rot)]
        self.base = None

    def set_mask(self, mask_words):
        self.base = [z ^ deobfuscate(mask_words[a & 7], self.rot[a]) for a, z in enumerate(self.z)]

    def keymask(self, a, key):
        m, act, k = 0, self.active[a], key[a & 0xff]
        for i in range(16):
            m |= (BIT(k, i & 7) if act[i] else BIT(FIXED, i)) << i
        return m

    def decrypt(self, key):
        return struct.pack('<%dH' % self.n, *[b ^ self.keymask(a, key) for a, b in enumerate(self.base)])


# --- the steps ---

def solve_fill(rom, permuted):
    """The linear system of the repeated (fill) blocks: unknowns the fill
    pattern (128 words), the FPGA mask (8 words), the key (256 bytes)."""
    F, MB, KB = 0, 2048, 2176
    repeats = collections.Counter(permuted[i:i + 16] for i in range(0, len(permuted), 16))
    eqs = collections.Counter()
    for a in range(rom.n):
        if repeats[permuted[(a // 8) * 16:(a // 8) * 16 + 16]] < 2:
            continue
        z, r, act = rom.z[a], rom.rot[a], rom.active[a]
        for i in range(16):
            row = 1 << (F + (a & 127) * 16 + i)
            row ^= 1 << (MB + (a & 7) * 16 + (SWAP[15 - i] - r) % 16)
            rhs = BIT(z, i)
            if act[i]:
                row ^= 1 << (KB + (a & 0xff) * 8 + (i & 7))
            else:
                rhs ^= BIT(FIXED, i)
            eqs[(row, rhs)] += 1
    basis = {}
    for (row, rhs), _ in sorted(eqs.items(), key=lambda kv: -kv[1]):
        while row:
            p = row.bit_length() - 1
            if p not in basis:
                basis[p] = (row, rhs)
                break
            br, bh = basis[p]
            row ^= br
            rhs ^= bh
    sol = [0] * 4224
    for p in sorted(basis):
        row, v = basis[p]
        rest = row & ~(1 << p)
        while rest:
            q = rest.bit_length() - 1
            v ^= sol[q]
            rest &= ~(1 << q)
        sol[p] = v
    mask = [sum(sol[MB + m * 16 + b] << b for b in range(16)) for m in range(8)]
    key = bytearray(sum(sol[KB + k * 8 + b] << b for b in range(8)) for k in range(256))
    known = {p - KB for p in basis if p >= KB}
    return mask, key, known


def fill_start(rom_bytes):
    top = collections.Counter(rom_bytes[i:i + 16] for i in range(0, len(rom_bytes), 16)).most_common(1)[0][0]
    s = rom_bytes.index(top)
    s -= s % 16
    pattern = rom_bytes[s:s + 256]
    while s >= 256 and rom_bytes[s - 256:s] == pattern:
        s -= 256
    return s


def vote(rom, key, words, plain_of, threshold=0.75, minimum=3, only=None):
    """Sets the key bits a set of known plaintext words decides."""
    votes = [[collections.Counter() for _ in range(8)] for _ in range(256)]
    for a in words:
        p, act, lo = plain_of(a), rom.active[a], a & 0xff
        for i in range(16):
            if act[i]:
                votes[lo][i & 7][BIT(p ^ rom.base[a], i)] += 1
    changed = 0
    for lo in range(256):
        for b in range(8):
            if only is not None and lo * 8 + b not in only:
                continue
            c = votes[lo][b]
            total = c[0] + c[1]
            if total >= minimum and max(c[0], c[1]) / total >= threshold:
                v = 1 if c[1] > c[0] else 0
                if BIT(key[lo], b) != v:
                    key[lo] ^= 1 << b
                    changed += 1
    return changed


def align_refs(rom, key, refs, code_end):
    """Known plaintext: regions of the references (their code, before their
    fill) found in the ROM at any offset, from 8-byte exact anchors."""
    dec = rom.decrypt(key)
    known = {}
    for ref in refs:
        index = collections.defaultdict(list)
        for i in range(0, len(ref) - 8, 2):
            index[ref[i:i + 8]].append(i)
        deltas = collections.Counter()
        for i in range(0, code_end - 8, 2):
            for j in index.get(dec[i:i + 8], ()):
                deltas[(i // 64, j - i)] += 1
        best = {}
        for (win, d), c in deltas.items():
            if c > best.get(win, (0, 0))[0]:
                best[win] = (c, d)
        for win, (_, d) in best.items():
            for i in range(max(0, win * 64 - 64), min(code_end, win * 64 + 128), 2):
                j = i + d
                if j < 16 or j + 18 > len(ref) or i < 16:
                    continue
                a, b = dec[i - 16:i + 18], ref[j - 16:j + 18]
                if sum(x == y for x, y in zip(a, b)) / len(a) >= 0.7:
                    known[i // 2] = struct.unpack_from('<H', ref, j)[0]
    return known


def bigram_refine(rom, key, free, refs):
    counts = [[1] * 256 for _ in range(256)]
    for ref in refs:
        for i in range(1, len(ref)):
            counts[ref[i - 1]][ref[i]] += 1
    lp = [[math.log(c / sum(row)) for c in row] for row in counts]
    touched = collections.defaultdict(set)
    for a in range(rom.n):
        act, lo = rom.active[a], a & 0xff
        for i in range(16):
            v = lo * 8 + (i & 7)
            if act[i] and v in free:
                touched[v].add(a)

    def word(a):
        return rom.base[a] ^ rom.keymask(a, key) if 0 <= a < rom.n else 0

    def score(addrs):
        s = 0.0
        for a in addrs:
            p, c, n = word(a - 1), word(a), word(a + 1)
            s += lp[p >> 8][c & 0xff] + lp[c & 0xff][c >> 8] + lp[c >> 8][n & 0xff]
        return s

    for _ in range(4):
        changed = 0
        for v in sorted(free):
            lo, b = divmod(v, 8)
            addrs = touched.get(v, ())
            before = score(addrs)
            key[lo] ^= 1 << b
            if score(addrs) > before:
                changed += 1
            else:
                key[lo] ^= 1 << b
        if not changed:
            break


def check_hash(rom_bytes, start):
    """MAME's calculate_hash: the 16-byte magic and the 4-byte hash are where
    the fill's pattern breaks; returns the shifts that give the stored hash."""
    pattern = rom_bytes[start:start + 256]
    odd = [i for i in range(start, len(rom_bytes), 4)
           if rom_bytes[i:i + 4] != pattern[(i - start) % 256:(i - start) % 256 + 4]]
    if len(odd) < 5:
        return None, odd
    at = odd[0]
    magic = struct.unpack_from('<4I', rom_bytes, at)
    stored, = struct.unpack_from('<I', rom_bytes, at + 16)
    values = struct.unpack('<%dI' % (len(rom_bytes) // 4), rom_bytes)
    for s1 in range(1, 33):
        for s2 in range(1, 33):
            h, off = 0, 0
            for v in values:
                if off == 4:
                    off = 0
                    continue
                off = off + 1 if magic[off & 3] == v else 0
                h = (h ^ ((v << s1) & 0xffffffff) ^ (h >> s2)) | 0x80
            if h == stored:
                return (s1, s2, at, stored), odd
    return None, odd


def elf_image(path, at, size):
    with open(path, 'rb') as f:
        d = f.read()
    if at is None:
        # Real36::loadBinTo36: movl $image, -0x30(%ebp) (c7 45 d0 imm32)
        syms = subprocess.run(['nm', path], capture_output=True, text=True).stdout
        m = re.search(r'^([0-9a-f]+) T _ZN6Real3611loadBinTo36EPKc$', syms, re.M)
        n = re.search(r'^([0-9a-f]+) T P2A_LoaderAdv_iLoadDataPointer$', syms, re.M)
        if not m or not n:
            sys.exit('no Real36::loadBinTo36 / P2A_LoaderAdv_iLoadDataPointer symbol: give --at and --size')
        func, load = int(m.group(1), 16), int(n.group(1), 16)
    phoff, = struct.unpack_from('<I', d, 0x1c)
    phnum, = struct.unpack_from('<H', d, 0x2c)

    def offset(va):
        for i in range(phnum):
            kind, off, vaddr, _, filesz = struct.unpack_from('<5I', d, phoff + 32 * i)
            if kind == 1 and vaddr <= va < vaddr + filesz:
                return off + va - vaddr
        sys.exit('address %#x not in the ELF' % va)

    if at is None:
        code = d[offset(func):offset(func) + 0x400]
        m = re.search(rb'\xc7\x45\xd0(....)', code, re.S)
        if not m:
            sys.exit('image address not found in loadBinTo36: give --at')
        at, = struct.unpack('<I', m.group(1))
        # P2A_LoaderAdv_iLoadDataPointer(image, size): the last
        # movl $size, 0x4(%esp) before its call
        call = next(i for i in range(len(code) - 5)
                    if code[i] == 0xe8 and func + i + 5 + struct.unpack_from('<i', code, i + 1)[0] == load)
        sizes = [s for s in re.finditer(rb'\xc7\x44\x24\x04(....)', code[:call], re.S)]
        size = size or struct.unpack('<I', sizes[-1].group(1))[0]
        print('image %#x, %#x bytes (Real36::loadBinTo36 %#x)' % (at, size, func))
    return d[offset(at):offset(at) + (size or 0x90000)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--elf')
    ap.add_argument('--at', type=lambda s: int(s, 0))
    ap.add_argument('--size', type=lambda s: int(s, 0))
    ap.add_argument('--image', help='the encrypted image (header and ROM) as a file')
    ap.add_argument('--ref', action='append', default=[], help='a decrypted IGS036 ROM (known plaintext)')
    ap.add_argument('out')
    args = ap.parse_args()
    if args.image:
        with open(args.image, 'rb') as f:
            image = f.read()
    elif args.elf:
        image = elf_image(args.elf, args.at, args.size)
    else:
        ap.error('--elf or --image')
    refs = []
    for path in args.ref:
        with open(path, 'rb') as f:
            r = f.read()
        refs.append(r[:fill_start(r)])

    permuted = fpga_perm(image[HEADER:])
    rom = Rom(permuted)
    mask, key, known = solve_fill(rom, permuted)
    rom.set_mask(mask)
    free = set(range(2048)) - known
    print('fill: %d of 2048 key bits' % len(known))
    if refs:
        bigram_refine(rom, key, free, refs)
        start = fill_start(rom.decrypt(key))
        for _ in range(2):
            plain = align_refs(rom, key, refs, start)
            changed = vote(rom, key, plain, plain.get, only=free)
            print('references: %d words known, %d key bits changed' % (len(plain), changed))
    dec = rom.decrypt(key)
    start = fill_start(dec)
    pattern = dec[start:start + 256]
    words = [a for a in range(start // 2, rom.n)]
    vote(rom, key, words, lambda a: struct.unpack_from('<H', pattern, (2 * a - start) % 256)[0],
         threshold=0.5, minimum=1)
    dec = rom.decrypt(key)
    with open(args.out, 'wb') as f:
        f.write(dec)
    mask_bytes = struct.pack('<8H', *mask)
    print('FPGA mask %s' % mask_bytes.hex())
    print('IGS036 key %s' % bytes(key).hex())
    found, odd = check_hash(dec, start)
    if found:
        print('%s: hash OK (%08x at %#x, shifts %d %d), fill from %#x'
              % (args.out, found[3], found[2], found[0], found[1], start))
        return 0
    print('%s: hash NOT matched (%d words break the fill from %#x)' % (args.out, len(odd), start))
    return 1


if __name__ == '__main__':
    sys.exit(main())
