#!/usr/bin/env python3
"""Big Buck Safari's data, rebuilt from Big Buck World's copy of it.

Safari's own data files are encrypted with keys from its dongle, whose
answers are not recorded. Big Buck World (a bundle of Safari and Pro) ships
Safari's data in bbwdata/bbs2 and its answers in hasp/. Both games derive a
file's key from the two secrets the dongle decrypts at startup (512 and 256
bytes) and the file's name:

  key = kdf(s512, name, 32)            .g3 and .tga files
  key = dongle(kdf(s512, name, 32))    .cf files (a recorded 32-byte answer)
  iv  = kdf(s256, name, 16)

and the file is a zlib stream, encrypted with Blowfish-CBC (16-byte key,
8-byte IV) by World and AES-256-CBC by Safari. Answering Safari's two
startup questions with World's secrets makes it derive World's keys: World's
files, re-encrypted with AES, and World's answers then work unchanged.

This writes, into Safari's game directory:
  bbsdata/ecf, bbsdata/ehd_pc   World's bbs2 files (and the shared ones they
                                use), re-encrypted for Safari
  bbsdata/exml                  World's telemetry files (already AES)
  bbsdata/snd                   World's bbs2 sounds, then the shared ones,
                                then Safari's own (plain files)
  hasp/                         World's answers and Safari's two startup ones
Safari's ecf, ehd_pc, exml and snd are moved to ORIGINALS first.

usage: rt-bbs-data.py <World's game dir> <Safari's game dir> <originals dir>
"""
import os
import re
import shutil
import struct
import sys
import warnings
import zlib

warnings.filterwarnings('ignore')
from cryptography.hazmat.primitives import padding  # noqa: E402
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes  # noqa: E402
from cryptography.hazmat.decrepit.ciphers.algorithms import Blowfish  # noqa: E402

# World's startup questions' answers (its hasp/), the secrets.
BBW_S512 = 'dfa64212bc2f4416ac7e0309958a6d6216aa7c4a297c9c16a49d8b5e1288ba87'
BBW_S256 = '50d7f97410277ac66524186262b3a42ef557956cb7239fd8519a1f6d5c14f508'
# Safari's startup questions, in its game (file offsets).
BBS_Q512 = 0x4644E0
BBS_Q256 = 0x4646E0


def kdf(secret, name, size):
    """The games' key derivation (Safari 0x81da6b4, World 0x823c30c)."""
    a, b = secret, name
    ia = (len(b) + size) % len(a)
    ib = (len(a) + size) % len(b)
    out = bytearray(size)
    for k in range(size):
        out[k] = b[ib] ^ a[ia]
        ia = (ia + b[ib] + k + size) % len(a)
        ib = (ib + a[ia] + k + size) % len(b)
    return bytes(out)


def patch_cf(name, stream):
    """World's scripts play its own attract and credits movies: Safari's."""
    edits = CF_EDITS.get(name)
    if not edits:
        return stream
    data = zlib.decompress(stream)
    text = data[4:]
    for old, new in edits:
        assert old in text, (name, old)
        text = text.replace(old, new)
    return zlib.compress(len(text).to_bytes(4, 'little') + text, 9)


CF_EDITS = {
    'title.cf': [
        (b'notOnline:\r\n    load(atmovieWorld)', b'notOnline:\r\n    load(atmovie)'),
        (b'online:\r\n    load(atmovieWorld)', b'online:\r\n    load(atmovieO)'),
    ],
    'at.cf': [(b'credmovieWorld', b'credmovie')],
}


PAGE = re.compile(rb'(\x02\x00\x03\x00|\x01\x00\x01\x00)(..)(..)(....)\x00\x00\x00\x00\x04\x00\x00\x00', re.S)


def pages(library):
    """A .g3 library's texture pages: (pixel offset, width, height, mode)."""
    for m in PAGE.finditer(library):
        w, h = struct.unpack('<HH', m.group(2) + m.group(3))
        mode = 'RGBA' if m.group(1)[0] == 2 else 'RGB'
        if 16 <= w <= 4096 and 16 <= h <= 4096 and m.end() + w * h * len(mode) <= len(library):
            yield m.end(), w, h, mode


def bbw_library(path, s512, s256):
    name = os.path.basename(path).encode()
    key, iv = kdf(s512, name, 32), kdf(s256, name, 16)
    with open(path, 'rb') as f:
        data = f.read()
    return zlib.decompressobj().decompress(Cipher(Blowfish(key[:16]), modes.CBC(iv[:8])).decryptor().update(data))


def standby_image(bbw, s512, s256):
    """World's "please stand by" screen with Safari's banner for its logo.

    The logo's area is filled with the map's colour around it and the map's
    grain (its clean left edge); the banner is the game select screen's.
    """
    from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat

    def page(name, index):
        library = bbw_library(os.path.join(bbw, 'bbwdata', 'ehd_pc', name), s512, s256)
        o, w, h, mode = list(pages(library))[index]
        return Image.frombytes(mode, (w, h), library[o:o + w * h * len(mode)])

    screen = page('standby.g3', 0)
    rgb = screen.convert('RGB')
    box = (40, 78, 600, 392)
    mask = Image.new('L', rgb.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(box, radius=60, fill=255)
    ring = Image.new('L', rgb.size, 0)
    ImageDraw.Draw(ring).rounded_rectangle((box[0] - 20, box[1] - 10, box[2] + 20, box[3] + 10), radius=70, fill=255)
    ring = ImageChops.subtract(ring, mask)
    filled = rgb.copy()
    filled.paste(Image.new('RGB', rgb.size, tuple(int(v) for v in ImageStat.Stat(rgb, ring).median)), (0, 0), mask)
    smooth = filled.filter(ImageFilter.GaussianBlur(30))
    strip = rgb.crop((0, 100, 40, 380))
    grain = ImageChops.subtract(strip, strip.filter(ImageFilter.GaussianBlur(6)), 1, 128)
    tile = Image.new('RGB', rgb.size, (128, 128, 128))
    for x in range(0, rgb.width, 40):
        tile.paste(grain if x // 40 % 2 == 0 else grain.transpose(Image.FLIP_LEFT_RIGHT), (x, 100))
    smooth = ImageChops.add(smooth, ImageChops.subtract(tile, Image.new('RGB', rgb.size, (128, 128, 128))), 1, 0)
    rgb = Image.composite(smooth, rgb, mask.filter(ImageFilter.GaussianBlur(14)))
    banner = page('seltitle.g3', 0).convert('RGB').crop((8, 258, 632, 448))
    banner = banner.resize((560, banner.height * 560 // banner.width), Image.LANCZOS)
    # On the select screen's black: that black is left out.
    cut = banner.convert('L').point(lambda v: 255 if v > 14 else 0).filter(ImageFilter.GaussianBlur(1))
    rgb.paste(banner, ((rgb.width - banner.width) // 2, 235 - banner.height // 2), cut)
    if screen.mode == 'RGBA':
        rgb.putalpha(screen.getchannel('A'))
    return rgb.tobytes()


def patch_g3(name, stream, bbw, s512, s256):
    """World's standby screen carries its logo: Safari's banner instead."""
    if name != 'standby.g3':
        return stream
    library = bytearray(zlib.decompress(stream))
    o, w, h, mode = next(pages(library))
    library[o:o + w * h * len(mode)] = standby_image(bbw, s512, s256)
    return zlib.compress(bytes(library), 9)


def reencrypt(path, out, hasp, s512, s256, bbw):
    name = os.path.basename(path)
    q, iv = kdf(s512, name.encode(), 32), kdf(s256, name.encode(), 16)
    key = q
    if name.endswith('.cf'):
        with open(os.path.join(hasp, q.hex()), 'rb') as f:
            key = f.read()
    with open(path, 'rb') as f:
        data = f.read()
    plain = Cipher(Blowfish(key[:16]), modes.CBC(iv[:8])).decryptor().update(data)
    unpad = padding.PKCS7(64).unpadder()
    stream = unpad.update(plain) + unpad.finalize()
    zlib.decompressobj().decompress(stream[:65536])  # raises on a wrong key
    stream = patch_cf(name, stream)
    stream = patch_g3(name, stream, bbw, s512, s256)
    pad = padding.PKCS7(128).padder()
    enc = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    with open(os.path.join(out, name), 'wb') as f:
        f.write(enc.update(pad.update(stream) + pad.finalize()) + enc.finalize())


def files(directory):
    return sorted(f for f in os.listdir(directory) if os.path.isfile(os.path.join(directory, f)))


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    bbw, bbs, originals = sys.argv[1:]
    hasp = os.path.join(bbw, 'hasp')
    with open(os.path.join(hasp, BBW_S512), 'rb') as f:
        s512 = f.read()
    with open(os.path.join(hasp, BBW_S256), 'rb') as f:
        s256 = f.read()
    data, bbs2 = os.path.join(bbs, 'bbsdata'), os.path.join(bbw, 'bbwdata', 'bbs2')

    os.makedirs(originals, exist_ok=True)
    for d in ('ecf', 'ehd_pc', 'exml', 'snd'):
        src = os.path.join(data, d)
        if os.path.isdir(src) and not os.path.exists(os.path.join(originals, d)):
            shutil.move(src, os.path.join(originals, d))
        os.makedirs(src, exist_ok=True)

    for d in ('ecf', 'ehd_pc'):
        out = os.path.join(data, d)
        sources = [os.path.join(bbs2, d, f) for f in files(os.path.join(bbs2, d))]
        have = {os.path.basename(p) for p in sources}
        shared = os.path.join(bbw, 'bbwdata', d)
        sources += [os.path.join(shared, f) for f in files(shared) if f not in have]
        for path in sources:
            reencrypt(path, out, hasp, s512, s256, bbw)
        print(d, len(sources), 'files')

    for f in ('telemLUT.xml', 'telemFields.xml'):
        shutil.copy(os.path.join(bbw, 'bbwdata', 'exml', f), os.path.join(data, 'exml', f))

    snd = os.path.join(data, 'snd')
    for src in (os.path.join(bbs2, 'snd'), os.path.join(bbw, 'bbwdata', 'snd'), os.path.join(originals, 'snd')):
        for root, _, names in os.walk(src):
            dst = os.path.join(snd, os.path.relpath(root, src))
            os.makedirs(dst, exist_ok=True)
            for n in names:
                if not os.path.exists(os.path.join(dst, n)):
                    shutil.copy(os.path.join(root, n), os.path.join(dst, n))

    out = os.path.join(bbs, 'hasp')
    os.makedirs(out, exist_ok=True)
    for f in files(hasp):
        shutil.copy(os.path.join(hasp, f), os.path.join(out, f))
    with open(os.path.join(bbs, 'game'), 'rb') as f:
        game = f.read()
    for offset, secret in ((BBS_Q512, s512), (BBS_Q256, s256)):
        with open(os.path.join(out, game[offset:offset + 32].hex()), 'wb') as f:
            f.write(secret)
    print('hasp', len(files(out)), 'answers')


if __name__ == '__main__':
    main()
