// Extract an IGS E2200 root filesystem (igs_rofs v2, hda2) by running the
// kernel's own igs_rofs driver in user space.
//
// Each E2200 kernel has its own keys, XOR constants and inode layout, and its
// AES does not match a stock AES, so rather than re-implementing the format
// this maps the vmlinux's segments at their addresses (0xc1000000: a 32-bit
// process on a 64-bit kernel can map there), patches the few kernel services
// the driver calls (locks, allocators, iget5_locked, kmap, the block read...)
// with jumps to the functions here, and calls init_igs_rofs_fs,
// igs_rofs_fill_super, igs_rofs_readdir, get_igs_rofs_inode and
// igs_rofs_readpage as the VFS would (regparm(3), the kernel's ABI).
//
// Build: gcc -m32 -O1 -fno-pie -no-pie -o igs-rofs igs-rofs.c
// Usage: igs-rofs vmlinux kallsyms.txt hda2.img outdir
//   kallsyms.txt: "address type name" lines (tools/igs-e2200-extract.py
//   writes it from the vmlinux's kallsyms).
//
// Kernel structure offsets (Linux 2.6.32, i386, as this driver uses them):
// inode i_ino 0x20, i_size 0x40, i_mode 0x72, i_sb 0xa4, i_state 0x138
// (I_NEW 8); super_block s_flags 0x30, s_root 0x38, s_fs_info 0x180; file
// f_path.dentry 0x0c, f_pos 0x24; dentry d_inode 0x10; page mapping 0x10,
// index 0x14; address_space host 0.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define R3 __attribute__((regparm(3)))

static uint8_t *P;
static size_t PL;
static int files, dirs, links, errors;

static struct
{
    char name[128];
    uint32_t addr;
} syms[60000];
static int nsyms;

static uint32_t sym(const char *name)
{
    for (int i = 0; i < nsyms; i++)
        if (!strcmp(syms[i].name, name))
            return syms[i].addr;
    fprintf(stderr, "no symbol %s\n", name);
    exit(1);
}

static void hook(const char *name, void *fn)
{
    uint8_t *at = (uint8_t *)sym(name);
    at[0] = 0xe9;
    uint32_t rel = (uint32_t)fn - ((uint32_t)at + 5);
    memcpy(at + 1, &rel, 4);
}

static void kmap_vmlinux(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); exit(1); }
    uint8_t eh[52];
    if (pread(fd, eh, sizeof(eh), 0) != sizeof(eh) || memcmp(eh, "\x7f" "ELF\x01", 5)) { fprintf(stderr, "%s: not an ELF32\n", path); exit(1); }
    uint32_t phoff;
    uint16_t phnum;
    memcpy(&phoff, eh + 28, 4);
    memcpy(&phnum, eh + 44, 2);
    for (int i = 0; i < phnum; i++)
    {
        uint32_t ph[8];
        pread(fd, ph, sizeof(ph), phoff + 32 * i);
        if (ph[0] != 1)
            continue;
        uint32_t va = ph[2] & ~0xfffu, pad = ph[2] - va, mem = (ph[5] + pad + 0xfff) & ~0xfffu;
        void *m = mmap((void *)va, mem, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (m == MAP_FAILED) { perror("mmap"); exit(1); }
        if (pread(fd, (void *)ph[2], ph[4], ph[1]) != (ssize_t)ph[4]) { perror("pread"); exit(1); }
    }
    close(fd);
}

// --- the kernel services the driver calls
static int knop(void) { return 0; }
static R3 void *kKmemCacheAlloc(void *cache, int flags) { (void)cache; (void)flags; return calloc(1, 0x1000); }
static R3 void *kVmalloc(unsigned long size) { return calloc(1, size); }
static R3 void kKfree(void *p) { (void)p; }
static void *rootInode;
static R3 void *kDAllocRoot(void *inode) { rootInode = inode; return calloc(1, 0x100); }
static R3 void *kIget5(void *sb, unsigned long hash, void *test, int R3 (*set)(void *, void *), void *data)
{
    (void)hash; (void)test;
    uint8_t *inode = calloc(1, 0x400);
    set(inode, data);
    *(void **)(inode + 0xa4) = sb;
    *(uint32_t *)(inode + 0x138) = 8; // I_NEW
    return inode;
}
static R3 const void *kRead(void *sb, uint32_t offset, uint32_t len)
{
    (void)sb;
    static uint8_t zero[0x4000];
    if ((uint64_t)offset + len > PL)
        return zero;
    return P + offset;
}
static uint8_t pageBuffer[0x1000];
static R3 void *kKmap(void *page) { (void)page; return pageBuffer; }

typedef R3 int (*InitFs)(void);
typedef R3 int (*FillSuper)(void *sb, void *data, int silent);
typedef R3 void *(*GetInode)(void *sb, const void *raw);
typedef R3 int (*ReadDir)(void *file, void *dirent, void *filldir);
typedef R3 int (*ReadPage)(void *file, void *page);

static FillSuper fillSuper;
static GetInode getInode;
static ReadDir readDir;
static ReadPage readPage;
static uint8_t *sb;

struct Entry
{
    char name[256];
    uint32_t pos;
};
static struct Entry *entries;
static int nentries, cap;

static R3 int filldir(void *buf, const char *name, int len, uint64_t offset, uint64_t ino, unsigned type)
{
    (void)buf; (void)ino; (void)type;
    if (nentries == cap)
        entries = realloc(entries, (cap = cap ? cap * 2 : 64) * sizeof(*entries));
    if (len > 255)
        len = 255;
    memcpy(entries[nentries].name, name, len);
    entries[nentries].name[len] = 0;
    entries[nentries].pos = (uint32_t)offset;
    nentries++;
    return 0;
}

static uint8_t *readFile(uint8_t *inode, uint32_t size)
{
    uint32_t pages = (size + 0xfff) >> 12;
    uint8_t *out = calloc((size_t)pages * 0x1000 + 1, 1);
    uint8_t mapping[16] = {0}, page[64] = {0};
    *(void **)mapping = inode;
    *(void **)(page + 0x10) = mapping;
    for (uint32_t i = 0; i < pages; i++)
    {
        *(uint32_t *)page = 1; // locked
        *(uint32_t *)(page + 0x14) = i;
        memset(pageBuffer, 0, sizeof(pageBuffer));
        readPage(NULL, page);
        if (!(*(uint32_t *)page & 8)) // PG_uptodate
            errors++;
        memcpy(out + (size_t)i * 0x1000, pageBuffer, 0x1000);
    }
    return out;
}

static void walk(uint8_t *inode, const char *path)
{
    uint16_t mode = *(uint16_t *)(inode + 0x72);
    uint32_t size = *(uint32_t *)(inode + 0x40);
    switch (mode & 0xf000)
    {
    case 0x4000:
    {
        mkdir(path, 0755);
        dirs++;
        uint8_t dentry[0x100] = {0}, file[0x100] = {0};
        *(void **)(dentry + 0x10) = inode;
        *(void **)(file + 0x0c) = dentry;
        int first = nentries;
        readDir(file, NULL, (void *)filldir);
        int last = nentries;
        uint32_t base = *(uint32_t *)(inode + 0x20);
        for (int i = first; i < last; i++)
        {
            char sub[4096];
            snprintf(sub, sizeof(sub), "%s/%s", path, entries[i].name);
            uint8_t *child = getInode(sb, P + base + entries[i].pos);
            if (!child || (uint32_t)child >= 0xfffff000u)
            {
                fprintf(stderr, "%s: no inode\n", sub);
                errors++;
                continue;
            }
            walk(child, sub);
        }
        nentries = first;
        chmod(path, mode & 07777);
        break;
    }
    case 0x8000:
    case 0xa000:
    {
        uint8_t *d = readFile(inode, size);
        if ((mode & 0xf000) == 0xa000)
        {
            d[size] = 0;
            unlink(path);
            if (symlink((char *)d, path))
                perror(path);
            links++;
        }
        else
        {
            FILE *f = fopen(path, "wb");
            if (!f) { perror(path); free(d); return; }
            fwrite(d, 1, size, f);
            fclose(f);
            chmod(path, (mode & 07777) | 0600);
            files++;
        }
        free(d);
        break;
    }
    default: // device nodes, fifos: not made without root
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        fprintf(stderr, "usage: igs-rofs vmlinux kallsyms.txt hda2.img outdir\n");
        return 1;
    }
    FILE *f = fopen(argv[2], "r");
    if (!f) { perror(argv[2]); return 1; }
    char type, name[256];
    unsigned addr;
    while (nsyms < (int)(sizeof(syms) / sizeof(syms[0])) && fscanf(f, "%x %c %127s", &addr, &type, name) == 3)
    {
        syms[nsyms].addr = addr;
        snprintf(syms[nsyms].name, sizeof(syms[nsyms].name), "%s", name);
        nsyms++;
    }
    fclose(f);

    kmap_vmlinux(argv[1]);
    hook("printk", knop);
    hook("register_filesystem", knop);
    hook("mutex_lock", knop);
    hook("mutex_unlock", knop);
    hook("_spin_lock", knop);
    hook("init_special_inode", knop);
    hook("unlock_new_inode", knop);
    hook("iput", knop);
    hook("kunmap", knop);
    hook("unlock_page", knop);
    hook("kfree", kKfree);
    hook("vmalloc", kVmalloc);
    hook("kmem_cache_alloc", kKmemCacheAlloc);
    hook("d_alloc_root", kDAllocRoot);
    hook("iget5_locked", kIget5);
    hook("igs_rofs_read", kRead);
    hook("kmap", kKmap);
    fillSuper = (FillSuper)sym("igs_rofs_fill_super");
    getInode = (GetInode)sym("get_igs_rofs_inode");
    readDir = (ReadDir)sym("igs_rofs_readdir");
    readPage = (ReadPage)sym("igs_rofs_readpage");

    f = fopen(argv[3], "rb");
    if (!f) { perror(argv[3]); return 1; }
    fseek(f, 0, SEEK_END);
    PL = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    P = malloc(PL);
    if (fread(P, 1, PL, f) != PL) { perror("read"); return 1; }
    fclose(f);

    if (((InitFs)sym("init_igs_rofs_fs"))() < 0)
    {
        fprintf(stderr, "init_igs_rofs_fs failed\n");
        return 1;
    }
    sb = calloc(1, 0x400);
    int r = fillSuper(sb, NULL, 0);
    if (r || !rootInode)
    {
        fprintf(stderr, "igs_rofs_fill_super failed (%d)\n", r);
        return 1;
    }
    walk(rootInode, argv[4]);
    printf("%d directories, %d files, %d links, %d errors\n", dirs, files, links, errors);
    return errors ? 2 : 0;
}
