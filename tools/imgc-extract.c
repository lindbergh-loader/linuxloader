/*
 * imgc-extract: decompress an HDD Raw Copy Tool .imgc image to a sparse raw
 * disk image.
 *
 * Format (see https://github.com/shizmob/unimgc for the original write-up):
 *   0x0000  header, 0x1000 bytes: five 0x100-byte Pascal strings (tool, version,
 *           model, revision, serial), then at 0x500 u64 sector_count, u64
 *           sector_size.
 *   blocks: char tag[4], u32 size (includes this 8-byte header)
 *     "omg!": u64 count of zero bytes
 *     "lol!": u16 dsize (if bit 15 set, dsize = (dsize & 0x7fff) |
 *             next_u16 << 15), then a modified-LZO stream.
 *
 * Zero runs and all-zero output chunks are skipped with lseek, so the output
 * only uses disk space for real data.
 *
 * build: cc -O2 -o imgc-extract tools/imgc-extract.c
 * usage: imgc-extract IN.imgc OUT.img
 */
#define _FILE_OFFSET_BITS 64
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static unsigned parse_len(const uint8_t **pp, const uint8_t *end, unsigned nbits)
{
    const uint8_t *p = *pp;
    unsigned mask = (1u << nbits) - 1, len = *p++ & mask;
    if (!len) {
        while (p < end && !*p) {
            len += 255;
            p++;
        }
        len += (p < end ? *p++ : 0) + mask;
    }
    *pp = p;
    return len;
}

/* returns bytes produced, or (size_t)-1 on a corrupt stream */
static size_t lzo_dec(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen)
{
    const uint8_t *p = in, *end = in + inlen;
    uint8_t *op = out, *oend = out + outlen;
    unsigned state = 0;

    while (p < end) {
        unsigned ins = *p, len, st;
        size_t dist;

        if (p == in && ins > 17) {
            len = ins - 17;
            p++;
            if (p + len > end || op + len > oend) return (size_t)-1;
            memcpy(op, p, len);
            op += len; p += len;
            state = 4;
            continue;
        }
        if (ins >= 64) {
            if (p + 2 > end) return (size_t)-1;
            len = ((ins >> 5) & 7) + 1;
            dist = ((size_t)p[1] << 3) + ((ins >> 2) & 7) + 1;
            st = ins & 3;
            p += 2;
        } else if (ins >= 32) {
            len = parse_len(&p, end, 5) + 2;
            if (p + 2 > end) return (size_t)-1;
            unsigned f = rd16(p);
            dist = (f >> 2) + 1;
            st = f & 3;
            p += 2;
        } else if (ins >= 16) {
            len = parse_len(&p, end, 3) + 2;
            if (p + 2 > end) return (size_t)-1;
            unsigned f = rd16(p);
            dist = ((size_t)((ins >> 3) & 1) << 14) + (f >> 2) + 0x4000;
            st = f & 3;
            p += 2;
        } else if (!state) {
            len = parse_len(&p, end, 4) + 3;
            if (p + len > end || op + len > oend) return (size_t)-1;
            memcpy(op, p, len);
            op += len; p += len;
            state = 4;
            continue;
        } else {
            return (size_t)-1;
        }
        state = st;
        if (dist > (size_t)(op - out)) return (size_t)-1;
        if (len > (size_t)(oend - op)) len = oend - op;
        for (uint8_t *s = op - dist; len--; ) *op++ = *s++;
        if (st > 0 && st < 4) {
            if (p + st > end || op + st > oend) return (size_t)-1;
            memcpy(op, p, st);
            op += st; p += st;
        }
    }
    return op - out;
}

static int all_zero(const uint8_t *b, size_t n)
{
    static const uint8_t z[4096];
    while (n >= sizeof z) {
        if (memcmp(b, z, sizeof z)) return 0;
        b += sizeof z; n -= sizeof z;
    }
    return !n || !memcmp(b, z, n);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s IN.imgc OUT.img\n", argv[0]);
        return 2;
    }
    FILE *in = fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); return 1; }
    int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { perror(argv[2]); return 1; }

    uint8_t hdr[0x1000];
    if (fread(hdr, 1, sizeof hdr, in) != sizeof hdr) { fprintf(stderr, "short header\n"); return 1; }
    uint64_t total = rd64(hdr + 0x500) * rd64(hdr + 0x508);
    fprintf(stderr, "model %.*s, %llu bytes\n", hdr[0x200], hdr + 0x201,
            (unsigned long long)total);

    size_t cap = 0, dcap = 0;
    uint8_t *buf = NULL, *dbuf = NULL;
    uint64_t pos = 0, blocks = 0;
    uint8_t bh[8];

    while (fread(bh, 1, 8, in) == 8) {
        uint32_t size = rd32(bh + 4);
        if (size < 8) { fprintf(stderr, "bad block size at block %llu\n", (unsigned long long)blocks); return 1; }
        size_t sz = size - 8;
        if (sz > cap) { cap = sz; buf = realloc(buf, cap); }
        if (fread(buf, 1, sz, in) != sz) { fprintf(stderr, "truncated block %llu\n", (unsigned long long)blocks); return 1; }

        if (!memcmp(bh, "omg!", 4)) {
            pos += rd64(buf);
        } else if (!memcmp(bh, "lol!", 4)) {
            size_t hl = 2, dsz = rd16(buf);
            if (dsz & 0x8000) { dsz = (dsz & 0x7fff) | (size_t)rd16(buf + 2) << 15; hl = 4; }
            if (dsz > dcap) { dcap = dsz; dbuf = realloc(dbuf, dcap); }
            size_t got = lzo_dec(buf + hl, sz - hl, dbuf, dsz);
            if (got != dsz) {
                fprintf(stderr, "block %llu at out 0x%llx: decoded %zd of %zu\n",
                        (unsigned long long)blocks, (unsigned long long)pos, (ssize_t)got, dsz);
                return 1;
            }
            for (size_t o = 0; o < dsz; o += 65536) {
                size_t n = dsz - o < 65536 ? dsz - o : 65536;
                if (!all_zero(dbuf + o, n) &&
                    pwrite(out, dbuf + o, n, pos + o) != (ssize_t)n) { perror("write"); return 1; }
            }
            pos += dsz;
        } else {
            fprintf(stderr, "unknown block tag %.4s\n", (char *)bh);
            return 1;
        }
        if (!(++blocks % 20000))
            fprintf(stderr, "\r%.1f%%", 100.0 * pos / total);
    }
    if (ftruncate(out, pos) < 0) { perror("ftruncate"); return 1; }
    fprintf(stderr, "\rdone: %llu blocks, %llu bytes%s\n", (unsigned long long)blocks,
            (unsigned long long)pos, pos == total ? "" : " (size differs from header!)");
    close(out);
    return 0;
}
