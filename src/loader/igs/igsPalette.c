// Paletted textures (GL_EXT_paletted_texture, GL_EXT_shared_texture_palette)
// for the IGS games, which today's GL drivers lack: the game uploads its
// sprites as 8-bit indices (GL_COLOR_INDEX8_EXT) and sets one shared palette
// (glColorTableEXT on GL_SHARED_TEXTURE_PALETTE_EXT) before drawing each
// one, so the driver's white textures stand in for them. The indices are
// kept here; a texture is expanded to RGBA, again, when it is drawn
// (glBegin) under another palette than the one it was expanded with.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <glad/gl.h>

#include "igs.h"
#include "../common/importHook.h"
#include "../log/log.h"

#define GL_COLOR_INDEX8_EXT 0x80e5
#define GL_SHARED_TEXTURE_PALETTE_EXT 0x81fb
#define PALETTE_SIZE 256

typedef struct
{
    unsigned char *indices; // NULL: not a paletted texture
    int width, height;
    uint32_t palette[PALETTE_SIZE]; // the palette it was expanded with
} PalettedTexture;

static PalettedTexture *textures;
static GLuint textureCount;
static GLuint bound;
static uint32_t palette[PALETTE_SIZE];

static void (*realBindTexture)(GLenum, GLuint);
static void (*realTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static void (*realDeleteTextures)(GLsizei, const GLuint *);
static void (*realBegin)(GLenum);
static void *(*realGetProcAddressARB)(const char *);

static PalettedTexture *texture(GLuint id, int create)
{
    if (id >= textureCount)
    {
        GLuint count = textureCount ? textureCount : 256;
        PalettedTexture *grown;

        if (!create)
            return NULL;
        while (count <= id)
            count *= 2;
        if (!(grown = realloc(textures, count * sizeof(*textures))))
            return NULL;
        memset(grown + textureCount, 0, (count - textureCount) * sizeof(*textures));
        textures = grown;
        textureCount = count;
    }
    return &textures[id];
}

// The bound texture expanded with the current palette.
static void expand(PalettedTexture *t)
{
    size_t n = (size_t)t->width * t->height;
    uint32_t *rgba = malloc(n * sizeof(*rgba));

    if (!rgba)
        return;
    for (size_t i = 0; i < n; i++)
        rgba[i] = palette[t->indices[i]];
    memcpy(t->palette, palette, sizeof(palette));
    realTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->width, t->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
}

__attribute__((force_align_arg_pointer)) static void bindTexture(GLenum target, GLuint id)
{
    if (target == GL_TEXTURE_2D)
        bound = id;
    realBindTexture(target, id);
}

__attribute__((force_align_arg_pointer)) static void texImage2D(GLenum target, GLint level, GLint internalFormat,
                                                                GLsizei width, GLsizei height, GLint border,
                                                                GLenum format, GLenum type, const void *pixels)
{
    PalettedTexture *t;

    if (target != GL_TEXTURE_2D || level != 0 || internalFormat != GL_COLOR_INDEX8_EXT || format != GL_COLOR_INDEX ||
        type != GL_UNSIGNED_BYTE || !pixels || !(t = texture(bound, 1)))
    {
        if ((t = texture(bound, 0)) && target == GL_TEXTURE_2D && level == 0)
        {
            free(t->indices);
            t->indices = NULL;
        }
        realTexImage2D(target, level, internalFormat, width, height, border, format, type, pixels);
        return;
    }
    free(t->indices);
    if (!(t->indices = malloc((size_t)width * height)))
        return;
    memcpy(t->indices, pixels, (size_t)width * height);
    t->width = width;
    t->height = height;
    expand(t);
}

__attribute__((force_align_arg_pointer)) static void deleteTextures(GLsizei n, const GLuint *ids)
{
    for (GLsizei i = 0; i < n; i++)
    {
        PalettedTexture *t = texture(ids[i], 0);
        if (t)
        {
            free(t->indices);
            t->indices = NULL;
        }
    }
    realDeleteTextures(n, ids);
}

// The shared palette: 256 RGBA entries, as the game sets them.
__attribute__((force_align_arg_pointer)) static void colorTable(GLenum target, GLenum internalFormat, GLsizei width,
                                                                GLenum format, GLenum type, const void *table)
{
    (void)internalFormat;
    if (target != GL_SHARED_TEXTURE_PALETTE_EXT || format != GL_RGBA || type != GL_UNSIGNED_BYTE || !table)
        return;
    if (width > PALETTE_SIZE)
        width = PALETTE_SIZE;
    memcpy(palette, table, (size_t)width * sizeof(*palette));
}

__attribute__((force_align_arg_pointer)) static void begin(GLenum mode)
{
    PalettedTexture *t = texture(bound, 0);

    if (t && t->indices && memcmp(t->palette, palette, sizeof(palette)))
        expand(t);
    realBegin(mode);
}

__attribute__((force_align_arg_pointer)) static void *getProcAddressARB(const char *name)
{
    if (name && !strcmp(name, "glColorTableEXT"))
        return (void *)colorTable;
    return realGetProcAddressARB(name);
}

void igsPaletteInstall(void)
{
    *(void **)&realBindTexture = hookExecutableImport("glBindTexture", (void *)bindTexture);
    *(void **)&realTexImage2D = hookExecutableImport("glTexImage2D", (void *)texImage2D);
    *(void **)&realDeleteTextures = hookExecutableImport("glDeleteTextures", (void *)deleteTextures);
    *(void **)&realBegin = hookExecutableImport("glBegin", (void *)begin);
    *(void **)&realGetProcAddressARB = hookExecutableImport("glXGetProcAddressARB", (void *)getProcAddressARB);
    if (!realBindTexture || !realTexImage2D || !realDeleteTextures || !realBegin || !realGetProcAddressARB)
        log_error("IGS: the paletted textures' GL calls are not all imported");
}
