// IGS games (see igs.h): their cabinet's paths, the BIOS check and the
// start-up they need.

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <glad/gl.h>

#include "igs.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"
#include "../log/log.h"
#include "../../minhook/include/MinHook.h"
#include "../patching/flowControl.h"

extern uint32_t gId;

static char gameDir[PATH_MAX];
static const IgsGame *current;

// The dump's Ogre GL renderer leaves its automatic texture transform
// uninitialised. On this host it contains heap data / NaNs, turning video
// and fixed-function UI textures into single sampled colours. Initialise
// it when Ogre selects the renderer, before any rendering. Shader-driven
// race materials do not use this fixed-function matrix.
// Root calls this inside libOgreMain, so executable-import hooks miss it.
void igsOgreSetRenderSystem(void *root, void *renderer)
    __asm__("_ZN4Ogre4Root15setRenderSystemEPNS_12RenderSystemE");

__attribute__((force_align_arg_pointer)) void igsOgreSetRenderSystem(void *root, void *renderer)
{
    static void (*real)(void *, void *);
    if (!real)
        *(void **)&real = dlsym(RTLD_NEXT, "_ZN4Ogre4Root15setRenderSystemEPNS_12RenderSystemE");
    if (renderer && current && current->ogreAutoTextureMatrix)
    {
        Dl_info info;
        // The offsets describe the supplied GL plugin, not other renderers.
        if (dladdr(*(void **)renderer, &info) && info.dli_fname && strstr(info.dli_fname, "RenderSystem_GL.so"))
        {
            unsigned char *state = (unsigned char *)renderer + current->ogreAutoTextureMatrix;
            float *matrix = (float *)(state + 4);
            *state = 0;
            for (int i = 0; i < 16; i++)
                matrix[i] = i % 5 == 0 ? 1.0f : 0.0f;
        }
    }
    if (real)
        real(root, renderer);
}

// IGS's Ogre 1.8 (Speed Driver 4) keeps a material's techniques by scheme in
// a fixed array. With none for the active scheme, and none from the scheme
// listener, Material::getBestTechnique takes the default scheme's slot
// without checking it: a material whose supported techniques are all in
// other schemes (here, those whose default technique is not supported) is
// dereferenced at 0x14. When that slot is empty, the first supported
// technique is returned instead (upstream Ogre takes the first scheme), in
// the function's own padding. The bytes are checked: other builds are left
// as they are.
static void ogreBestTechniqueFallback(void)
{
    static const unsigned char original[] = {0x8b, 0xbf, 0xa0, 0x00, 0x00, 0x00, 0x89, 0x7d,
                                             0xec, 0xe9, 0x10, 0xff, 0xff, 0xff};
    unsigned char *f = dlsym(RTLD_DEFAULT, "_ZN4Ogre8Material16getBestTechniqueEtPKNS_10RenderableE");
    if (!f || memcmp(f + 0x146, original, sizeof(original)) || f[0xfb] != 0x90 || f[0x16a] != 0x8d)
        return;
    // 0x146: mov 0xa0(%edi),%eax; test %eax,%eax; jne 0x11a (stores it,
    // goes on); jmp 0x16a
    patchMemoryFromString((size_t)f + 0x146, "8b87a000000085c075caeb189090");
    // 0x16a: mov 0x6c(%edi),%eax (mSupportedTechniques); jmp 0xfb
    patchMemoryFromString((size_t)f + 0x16a, "8b476ceb8c");
    // 0xfb: mov (%eax),%edx; jmp 0x106 (returns it)
    patchMemoryFromString((size_t)f + 0xfb, "8b10eb07");
}

// IGS_OGRE_LOG=1 (debugging): Ogre's log, which the games keep to
// themselves (no Ogre.log), on the loader's output. Ogre::Log::logMessage's
// message is a gcc 4 std::string, a pointer to its characters.
static void (*realOgreLogMessage)(void *, const char *const *, int, int);

__attribute__((force_align_arg_pointer)) static void ogreLogMessage(void *log, const char *const *message, int level,
                                                                    int maskDebug)
{
    if (message && *message)
        printf("IGS Ogre: %s\n", *message);
    realOgreLogMessage(log, message, level, maskDebug);
}

// Speed Driver 4's UI (Scaleform's GL renderer, libgfxrender_gl): its
// filter shaders (blur, glow, shadow) sample with texture2DLod in fragment
// shaders, which NVIDIA's GLSL takes and Mesa's only with
// GL_ARB_shader_texture_lod enabled. One failed shader and Scaleform's
// renderer does not start: no UI at all (the menus look frozen behind it).
// Its GL functions are its Extensions' (glXGetProcAddressARB): once loaded,
// its glShaderSource is replaced by one adding the extension.
static void (*scaleformRealShaderSource)(unsigned, int, const char *const *, const int *);
static void (*scaleformGetShaderiv)(unsigned, unsigned, int *);

__attribute__((force_align_arg_pointer)) static void scaleformShaderSource(unsigned shader, int count,
                                                                           const char *const *strings, const int *lengths)
{
    static const char directive[] = "#extension GL_ARB_shader_texture_lod : enable\n";
    size_t size = 1;
    int type = 0;
    char *source, *at;

    for (int i = 0; i < count; i++)
        size += lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
    if (scaleformGetShaderiv)
        scaleformGetShaderiv(shader, 0x8b4f, &type); // GL_SHADER_TYPE
    if (type != 0x8b30 || !(source = malloc(size + sizeof(directive)))) // GL_FRAGMENT_SHADER
    {
        scaleformRealShaderSource(shader, count, strings, lengths);
        return;
    }
    at = source;
    for (int i = 0; i < count; i++)
    {
        size_t n = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
        memcpy(at, strings[i], n);
        at += n;
    }
    *at = '\0';
    if (strstr(source, "texture2DLod") && !strstr(source, "GL_ARB_shader_texture_lod"))
    {
        // after the #version line, if any
        char *line = strncmp(source, "#version", 8) ? source : strchr(source, '\n');
        line = line == source ? source : line ? line + 1 : source + strlen(source);
        memmove(line + sizeof(directive) - 1, line, strlen(line) + 1);
        memcpy(line, directive, sizeof(directive) - 1);
    }
    const char *one = source;
    scaleformRealShaderSource(shader, 1, &one, NULL);
    free(source);
}

static int (*scaleformRealExtensionsInit)(void *);

__attribute__((force_align_arg_pointer)) static int scaleformExtensionsInit(void *extensions)
{
    int ok = scaleformRealExtensionsInit(extensions);
    void *gl = dlopen("libGL.so.1", RTLD_LAZY | RTLD_NOLOAD);
    void *(*gpa)(const unsigned char *) = gl ? dlsym(gl, "glXGetProcAddressARB") : NULL;
    if (!gpa)
        return ok;
    *(void **)&scaleformGetShaderiv = gpa((const unsigned char *)"glGetShaderiv");
    void *names[] = {gpa((const unsigned char *)"glShaderSource"), gpa((const unsigned char *)"glShaderSourceARB")};
    void **slot = extensions;
    // its pointers, among the object's first words
    for (int i = 0; i < 512; i++)
        for (int n = 0; n < 2; n++)
            if (names[n] && slot[i] == names[n])
            {
                *(void **)&scaleformRealShaderSource = slot[i];
                slot[i] = (void *)scaleformShaderSource;
            }
    if (!scaleformRealShaderSource)
        log_error("IGS: Scaleform's glShaderSource not found, its UI's filter shaders left as they are");
    return ok;
}

static void scaleformShadersInstall(void)
{
    void *f = dlsym(RTLD_DEFAULT, "_ZN9Scaleform6Render2GL10Extensions4InitEv");
    if (f && MH_CreateHook(f, (void *)scaleformExtensionsInit, (void **)&scaleformRealExtensionsInit) != MH_OK)
        log_error("IGS: cannot hook Scaleform's GL extensions");
}

// Power Truck's resources.cfg puts the trucks' directories (../data/car/bus
// and the like) in its Kuruma group, and the car selection adds them to
// groups of its own, which ArchiveManager shares by name. Leaving the car
// selection, the game unloads its groups' archives (ArchiveManager::unload
// deletes, no reference count): Kuruma keeps the freed ones, and the next
// resource search (the race's scene) calls through them. The game's
// unloads are left out: its archives stay loaded, and a later load of the
// same name finds them.
__attribute__((force_align_arg_pointer)) static void ogreKeepArchive(void *manager, const void *name)
{
    (void)manager;
    (void)name;
}

static void ogreLogInstall(void)
{
    void *f = dlsym(RTLD_DEFAULT, "_ZN4Ogre3Log10logMessageERKSsNS_15LogMessageLevelEb");
    if (getenv("IGS_OGRE_LOG") && f && MH_CreateHook(f, (void *)ogreLogMessage, (void **)&realOgreLogMessage) != MH_OK)
        log_error("IGS: cannot hook Ogre's log");
}

const IgsGame *igsCurrentGame(void)
{
    static uint32_t cachedId;
    static const IgsGame *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = igsGetGame(gId);
    }
    return game;
}

int isIgsGame(void)
{
    return igsCurrentGame() != NULL;
}

// The Speed Rider packages keep Ogre's plugins in libso/, while their
// plugin list (ogrePlugins) names the cabinet's directory (PluginFolder=.):
// the game reads a copy naming libso/, removed at exit.
static char ogrePlugins[PATH_MAX];

__attribute__((destructor)) static void removeOgrePlugins(void)
{
    if (ogrePlugins[0])
        unlink(ogrePlugins);
}

static void ogrePluginsPrepare(void)
{
    char path[PATH_MAX], line[PATH_MAX + 32];
    FILE *in, *out;
    int fd;

    snprintf(path, sizeof(path), "%s/%s", gameDir, current->ogrePlugins);
    if (!(in = fopen(path, "r")))
        return;
    strcpy(ogrePlugins, "/tmp/linuxloader-sr-plugins.XXXXXX");
    if ((fd = mkstemp(ogrePlugins)) < 0 || !(out = fdopen(fd, "w")))
    {
        if (fd >= 0)
            close(fd);
        removeOgrePlugins();
        ogrePlugins[0] = '\0';
        fclose(in);
        return;
    }
    while (fgets(line, sizeof(line), in))
        if (!strncmp(line, "PluginFolder=", 13))
            fprintf(out, "PluginFolder=%s/libso\n", gameDir);
        else
            fputs(line, out);
    fclose(in);
    if (fclose(out))
    {
        removeOgrePlugins();
        ogrePlugins[0] = '\0';
    }
}

static int underDir(const char *path, const char *dir)
{
    size_t n = strlen(dir);

    return !strncmp(path, dir, n) && (path[n] == '/' || path[n] == '\0');
}

const char *igsRedirectPath(const char *path, char *buf, size_t size)
{
    const IgsGame *g = igsCurrentGame();

    if (!g || !path || !gameDir[0])
        return path;
    if (g->ogrePlugins && ogrePlugins[0] && !strcmp(path, g->ogrePlugins))
        return ogrePlugins;
    if (g->sd2Rom)
        while (path[0] == '/' && path[1] == '/')
            path++;
    // The BIOS reader and the security device: there, never read (the BIOS
    // check is the loader's, the device only opened). The OSS mixer is
    // common/ossDsp.c's.
    if (!strcmp(path, "/dev/roio") || !strcmp(path, "/dev/pccard0"))
        return "/dev/null";
    // The cabinet's RS232 devices (Speed Rider 2's card dealer and lights)
    // are none of the host's serial ports: run as root (Batocera) the game
    // opened the PC's and waited on them for good. Not there, as for a user
    // outside the dialout group.
    if (!strncmp(path, "/dev/ttyS", 9))
        return "/dev/null/ttyS";
    // Speed Driver looks for its motherboard's name in the BIOS, through
    // /dev/mem, to pick its sound card's mixer settings: none found, the
    // default ones.
    if (g->a27Rom && !strcmp(path, "/dev/mem"))
        return "/dev/zero";
    if (g->rootPath && underDir(path, g->rootPath))
    {
        snprintf(buf, size, "%s%s", gameDir, path + strlen(g->rootPath));
        return buf;
    }
    for (const char *const *dir = g->dataDirs; *dir; dir++)
        if (underDir(path, *dir))
        {
            snprintf(buf, size, "%s%s", gameDir, path);
            return buf;
        }
    return path;
}

// The BIOS check reads the motherboard's BIOS through /dev/roio and finds it
// in its table of the cabinets' boards: it is the one the descriptor names.
__attribute__((force_align_arg_pointer)) static int biosCheck(void *table, int board)
{
    (void)table;
    (void)board;
    return current->biosBoard;
}

// The game asks for full screen at its size (frameWidth x frameHeight, its
// cabinet's monitor): SDL 1.2 would switch the X server to it, which today's
// drivers refuse. It gets a window of that size, or, with [Display]
// FULLSCREEN or a larger configured size, full screen at the screen's
// ([Display] WIDTH x HEIGHT when larger than the game's, else the X
// screen's), the game's frame scaled to it at each swap
// (graphics/frameScale.h).
#define SDL12_FULLSCREEN 0x80000000u

static void *(*realSetVideoMode)(int, int, int, unsigned int);
static int screenWidth, screenHeight; // scaled to: 0, none
static int windowWidth, windowHeight; // the game's window (SDL linked): 0, unknown

__attribute__((force_align_arg_pointer)) static void *setVideoMode(int width, int height, int bpp, unsigned int flags)
{
    EmulatorConfig *config = getConfig();
    int larger = config->width > width || config->height > height;
    int w = config->width, h = config->height;

    if (!config->fullscreen && !larger)
        return realSetVideoMode(width, height, bpp, flags & ~SDL12_FULLSCREEN);
    if (!larger && !igsScreenSize(&w, &h))
    {
        w = width;
        h = height;
    }
    if (w != width || h != height)
    {
        frameScaleSetFrame(current->frameWidth, current->frameHeight, NULL);
        screenWidth = w;
        screenHeight = h;
    }
    printf("IGS: %dx%d asked, full screen at %dx%d\n", width, height, w, h);
    return realSetVideoMode(w, h, bpp, flags | SDL12_FULLSCREEN);
}

// A game linking SDL 1.2 (Speed Driver): its video mode set through the
// import, then GL loaded for the loader's wrappers (the context is current).
static void loadGl(void);

// Speed Driver draws both seats' screens side by side: by default ([Display]
// SEATS 1) the window shows seat 1's, the left half of the frame (SEATS 2:
// both).
__attribute__((force_align_arg_pointer)) static void *importedSetVideoMode(int width, int height, int bpp, unsigned int flags)
{
    void *surface;

    if (getConfig()->seats != 2 && width == current->frameWidth && width >= 2 * height)
    {
        frameScaleSetFrame(width, height, NULL);
        frameScaleSetCrop(0, 0, width / 2, height);
        surface = setVideoMode(width / 2, height, bpp, flags);
        if (surface && !screenWidth)
        {
            // windowed: the window is half the frame
            screenWidth = width / 2;
            screenHeight = height;
        }
        if (surface)
        {
            loadGl();
            windowWidth = screenWidth;
            windowHeight = screenHeight;
        }
        return surface;
    }
    surface = setVideoMode(width, height, bpp, flags);

    if (surface)
    {
        loadGl();
        windowWidth = width;
        windowHeight = height;
    }
    return surface;
}

// The game's SDL is static: it loads libGL itself and takes glXMakeCurrent
// from it with dlsym, past the loader's. The loader's GL wrappers call
// through glad's entry points, loaded once the context is current.
static void *(*realDlsym)(void *, const char *);
static int (*realMakeCurrent)(void *, unsigned long, void *);

__attribute__((force_align_arg_pointer)) static int makeCurrent(void *dpy, unsigned long drawable, void *ctx)
{
    int result = realMakeCurrent(dpy, drawable, ctx);

    if (result && ctx)
        loadGl();
    return result;
}

static void loadGl(void)
{
    static int glLoaded;
    GLADloadfunc getProcAddress;

    if (glLoaded)
        return;
    glLoaded = 1;
    getProcAddress = (GLADloadfunc)dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    if (!getProcAddress || !gladLoadGL(getProcAddress))
        log_error("IGS: cannot load the GL entry points");
}

// The game's SDL carries its own copies of Xlib extensions (VidMode, DGA,
// Xinerama), built without Xlib's thread locks: they call _XReply with the
// display unlocked. Today's libX11 always locks (XInitThreads is implicit),
// and _XReply's unlock and relock around its wait leaves the display locked
// for good: the next Xlib call (SDL's XCreateColormap) waits forever. The game
// draws from its main thread only: its display goes unlocked, as in its day.
static void *(*realXOpenDisplay)(const char *);

__attribute__((force_align_arg_pointer)) static void *gameXOpenDisplay(const char *name)
{
    void *dpy = realXOpenDisplay(name);

    if (dpy)
        igsUnlockDisplay(dpy);
    return dpy;
}

// IGS_SHOT=<directory> (debugging): a frame every 4 s (IGS_SHOT_PERIOD, in
// seconds, fractions too) from the first one on (IGS_SHOT_FROM seconds later)
// saved there as a PPM,
// read back before the swap, scaled (a capture of the window comes out black under
// gamescope; Xvfb's GLX refuses the game's SDL).
static void (*realSwapBuffers)(void *, unsigned long);
static unsigned int frames;

unsigned int igsFrames(void)
{
    return frames;
}

// The frame's end, before its swap: scaled, saved (IGS_SHOT).
static void frameDone(void)
{
    static double next = -1;
    static int count;
    const char *dir = getenv("IGS_SHOT");
    struct timespec ts;
    double now;
    GLint viewport[4];

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = ts.tv_sec + ts.tv_nsec / 1e9;
    if (next < 0)
        next = now + (getenv("IGS_SHOT_FROM") ? atof(getenv("IGS_SHOT_FROM")) : 0);
    if (screenWidth)
        frameScalePresent(0, 0, screenWidth, screenHeight, getConfig()->borderEnabled,
                          getConfig()->whiteBorderPercentage, getConfig()->blackBorderPercentage);
    if (dir && now >= next && glad_glReadPixels)
    {
        char path[PATH_MAX];
        unsigned char *pixels;
        FILE *f;

        next = now + (getenv("IGS_SHOT_PERIOD") ? atof(getenv("IGS_SHOT_PERIOD")) : 4);
        glad_glGetIntegerv(GL_VIEWPORT, viewport);
        if (screenWidth)
        {
            viewport[0] = viewport[1] = 0;
            viewport[2] = screenWidth;
            viewport[3] = screenHeight;
        }
        else if (windowWidth)
        {
            // Speed Driver draws its two seats' views side by side
            viewport[0] = viewport[1] = 0;
            viewport[2] = windowWidth;
            viewport[3] = windowHeight;
        }
        printf("IGS: shot %d\n", count);
        snprintf(path, sizeof(path), "%s/igs_%04d.ppm", dir, count++);
        pixels = malloc((size_t)viewport[2] * viewport[3] * 3);
        if (pixels && (f = fopen(path, "wb")))
        {
            GLint read = 0;

            // The window's own (the scaling keeps the game's frame bound).
            glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
            glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glad_glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glad_glReadBuffer(GL_BACK);
            glad_glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGB, GL_UNSIGNED_BYTE, pixels);
            glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
            fprintf(f, "P6\n%d %d\n255\n", viewport[2], viewport[3]);
            for (int y = viewport[3]; y-- > 0;)
                fwrite(pixels + (size_t)y * viewport[2] * 3, 1, (size_t)viewport[2] * 3, f);
            fclose(f);
        }
        free(pixels);
    }
    frames++;
}

__attribute__((force_align_arg_pointer)) static void swapBuffers(void *dpy, unsigned long drawable)
{
    frameDone();
    realSwapBuffers(dpy, drawable);
}

// The Ogre games' frame, drawn at the cabinet's size in the bottom left of a
// larger window (igsX11.c), stretched to all of it (kept to its shape with
// [Display] KEEP_ASPECT_RATIO): copied out to a texture, drawn back as a quad
// with the fixed pipeline (the window is multisampled: no scaled blit into
// it), the game's state put back after.
static void stretchFrame(void)
{
    static GLuint texture, framebuffer;
    int width, height, x = 0, y = 0, w, h;
    GLint draw, read, readBuffer, drawBuffer, program = 0, activeTexture = GL_TEXTURE0, bound = 0;

    if (!igsWindowSize(&width, &height))
        return;
    loadGl();
    if (!glad_glBlitFramebuffer || !glad_glGenFramebuffers)
        return;
    w = width;
    h = height;
    if (getConfig()->keepAspectRatio)
    {
        if ((long)width * current->frameHeight > (long)height * current->frameWidth)
            w = (int)((long)height * current->frameWidth / current->frameHeight);
        else
            h = (int)((long)width * current->frameHeight / current->frameWidth);
        x = (width - w) / 2;
        y = (height - h) / 2;
    }

    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    if (!texture)
    {
        glad_glGenTextures(1, &texture);
        glad_glBindTexture(GL_TEXTURE_2D, texture);
        glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, current->frameWidth, current->frameHeight, 0, GL_RGBA,
                          GL_UNSIGNED_BYTE, NULL);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glad_glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
        glad_glGenFramebuffers(1, &framebuffer);
        glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glad_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    }
    glad_glPushAttrib(GL_ALL_ATTRIB_BITS);
    glad_glDisable(GL_SCISSOR_TEST);
    glad_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glad_glGetIntegerv(GL_READ_BUFFER, &readBuffer);
    glad_glReadBuffer(GL_BACK);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glad_glBlitFramebuffer(0, 0, current->frameWidth, current->frameHeight, 0, 0, current->frameWidth,
                           current->frameHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glad_glReadBuffer((GLenum)readBuffer);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glad_glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
    glad_glDrawBuffer(GL_BACK);
    glad_glClearColor(0.f, 0.f, 0.f, 1.f);
    glad_glClear(GL_COLOR_BUFFER_BIT);

    if (glad_glUseProgram)
    {
        glad_glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glad_glUseProgram(0);
    }
    if (glad_glActiveTexture)
    {
        glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        for (int unit = 7; unit >= 0; unit--)
        {
            glad_glActiveTexture(GL_TEXTURE0 + unit);
            glad_glDisable(GL_TEXTURE_2D);
        }
    }
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glViewport(x, y, w, h);
    glad_glDisable(GL_DEPTH_TEST);
    glad_glDisable(GL_STENCIL_TEST);
    glad_glDisable(GL_BLEND);
    glad_glDisable(GL_ALPHA_TEST);
    glad_glDisable(GL_CULL_FACE);
    glad_glDisable(GL_LIGHTING);
    glad_glDisable(GL_FOG);
    glad_glEnable(GL_TEXTURE_2D);
    glad_glBindTexture(GL_TEXTURE_2D, texture);
    glad_glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glad_glColor4f(1.f, 1.f, 1.f, 1.f);
    glad_glBegin(GL_QUADS);
    glad_glTexCoord2f(0.f, 0.f);
    glad_glVertex2f(-1.f, -1.f);
    glad_glTexCoord2f(1.f, 0.f);
    glad_glVertex2f(1.f, -1.f);
    glad_glTexCoord2f(1.f, 1.f);
    glad_glVertex2f(1.f, 1.f);
    glad_glTexCoord2f(0.f, 1.f);
    glad_glVertex2f(-1.f, 1.f);
    glad_glEnd();
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPopMatrix();
    glad_glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    if (glad_glActiveTexture)
        glad_glActiveTexture((GLenum)activeTexture);
    if (glad_glUseProgram)
        glad_glUseProgram((GLuint)program);

    glad_glDrawBuffer((GLenum)drawBuffer);
    glad_glPopAttrib();
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
}

void igsBeforeSwap(void)
{
    if (current && current->a36Rom && current->frameWidth)
        stretchFrame();
    if (current && (current->a36Rom || current->sr1Rom))
        frameDone();
}

static void (*realSdlSwapBuffers)(void);

__attribute__((force_align_arg_pointer)) static void sdlSwapBuffers(void)
{
    frameDone();
    realSdlSwapBuffers();
}

__attribute__((force_align_arg_pointer)) static void *gameDlsym(void *handle, const char *name)
{
    void *symbol = realDlsym(handle, name);

    if (symbol && name && !strcmp(name, "glXMakeCurrent"))
    {
        *(void **)&realMakeCurrent = symbol;
        return (void *)makeCurrent;
    }
    if (symbol && name && !strcmp(name, "glXSwapBuffers"))
    {
        *(void **)&realSwapBuffers = symbol;
        return (void *)swapBuffers;
    }
    return symbol;
}

// The frame scaling's GL entry points in place of the game's imports; they
// change nothing until setVideoMode asks for scaling.
static void *glProcAddress(const char *name)
{
    static void *(*get)(const char *);

    if (!get)
        *(void **)&get = dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    return get ? get(name) : NULL;
}

static void frameScaleInstall(void)
{
    static const char *const wrapped[] = {"glViewport", "glDrawBuffer", "glReadBuffer", "glGetIntegerv"};

    frameScaleInit(glProcAddress, getConfig()->keepAspectRatio);
    for (size_t i = 0; i < sizeof(wrapped) / sizeof(wrapped[0]); i++)
    {
        void *wrapper = frameScaleWrapper(wrapped[i]);
        if (wrapper)
            hookExecutableImport(wrapped[i], wrapper);
    }
}

// The cabinet's commands (Speed Driver: its sound card's mixer set with
// umix, the clock written to the RTC with hwclock): none on the host.
__attribute__((force_align_arg_pointer)) static int gameSystem(const char *command)
{
    if (command)
        printf("IGS: not run: %s", command);
    return 0;
}

static FILE *(*realPopen)(const char *, const char *);
static int (*realPclose)(FILE *);
static FILE *cabinetPipes[8];

// SD3's mac2uuid reads the first line of the old net-tools output, even
// offline. A stable locally administered cabinet identity avoids depending
// on the host's interface names or net-tools version. Curl checks have no
// reply: keep the dump's news and filter files.
__attribute__((force_align_arg_pointer)) static FILE *cabinetPopen(const char *command, const char *mode)
{
    const char *output = NULL;
    if (command && mode && !strcmp(mode, "r"))
    {
        if (!strcmp(command, "ifconfig eth1"))
            output = "eth1      Link encap:Ethernet  HWaddr 02:00:00:00:00:01\n";
        else if (current->sd2Rom && !strcmp(command, "ifconfig eth0"))
            output = ""; // SD2's two legacy parsers accept an offline interface.
        else if (!strncmp(command, "./curl ", 7))
            output = "";
    }
    if (output)
        for (size_t i = 0; i < sizeof(cabinetPipes) / sizeof(cabinetPipes[0]); i++)
            if (!cabinetPipes[i])
            {
                cabinetPipes[i] = fmemopen((void *)output, strlen(output), "r");
                return cabinetPipes[i];
            }
    return realPopen(command, mode);
}

__attribute__((force_align_arg_pointer)) static int cabinetPclose(FILE *stream)
{
    for (size_t i = 0; i < sizeof(cabinetPipes) / sizeof(cabinetPipes[0]); i++)
        if (stream && cabinetPipes[i] == stream)
        {
            cabinetPipes[i] = NULL;
            return fclose(stream);
        }
    return realPclose(stream);
}

// The game's keyboard reads: no key pressed.
__attribute__((force_align_arg_pointer)) static int noKey(int key)
{
    (void)key;
    return 0;
}

// OIS 1.0's evdev buffers predate modern kernels. Cabinet input comes
// through the emulated board, so return an empty C++ vector of joysticks.
typedef struct
{
    void *begin, *end, *capacity;
} IgsEmptyVector;

__attribute__((force_align_arg_pointer)) static IgsEmptyVector noSr1Joysticks(void)
{
    return (IgsEmptyVector){NULL, NULL, NULL};
}

int igsInit(void)
{
    char path[PATH_MAX];

    current = igsCurrentGame();
    // Started in the game's directory, which the cabinet runs it from too.
    if (!getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';
    for (const char *const *dir = current->dataDirs; *dir; dir++)
    {
        snprintf(path, sizeof(path), "%s%s", gameDir, *dir);
        mkdir(path, 0755);
    }

    if (current->sr1Rom)
    {
        hookExecutableImport("system", (void *)gameSystem);
        ogrePluginsPrepare();
        void *scan = dlsym(RTLD_DEFAULT, "_ZN3OIS13LinuxJoyStick9_scanJoysEv");
        if (scan)
            detourFunction((uintptr_t)scan, noSr1Joysticks);
        igsSr1Install(current, gameDir);
        // The bike and rider materials are vp40/fp40 Cg programs.
        igsNvProgramInstall();
        return 0;
    }

    if (current->a36Rom)
    {
        if (current->ogrePlugins)
            ogrePluginsPrepare();
        hookExecutableImport("system", (void *)gameSystem);
        *(void **)&realPopen = hookExecutableImport("popen", (void *)cabinetPopen);
        *(void **)&realPclose = hookExecutableImport("pclose", (void *)cabinetPclose);
        igsA36Install(current, gameDir);
        igsSd3InputInstall(current);
        igsNvProgramInstall();
        ogreBestTechniqueFallback();
        ogreLogInstall();
        scaleformShadersInstall();
        if (current->ogreKeepArchives && !hookExecutableImport("_ZN4Ogre14ArchiveManager6unloadERKSs", (void *)ogreKeepArchive))
            log_error("IGS: no ArchiveManager::unload import");
        if (current->frameWidth)
            igsWindowAttributesInstall(current->frameWidth, current->frameHeight);
        return 0;
    }

    if (current->a27Rom || current->sd2Rom)
    {
        // SDL 1.2 linked: its video mode and swap through the imports. The
        // host's is sdl12-compat, whose own OpenGL framebuffer (its scaling)
        // would cover the loader's scaled frame at each swap.
        setenv("SDL12COMPAT_OPENGL_SCALING", "0", 1);
        *(void **)&realSetVideoMode = hookExecutableImport("SDL_SetVideoMode", (void *)importedSetVideoMode);
        *(void **)&realSdlSwapBuffers = hookExecutableImport("SDL_GL_SwapBuffers", (void *)sdlSwapBuffers);
        if (!realSetVideoMode || !realSdlSwapBuffers)
            log_error("IGS: no SDL_SetVideoMode or SDL_GL_SwapBuffers import");
        hookExecutableImport("system", (void *)gameSystem);
        if (current->sd2Rom)
        {
            *(void **)&realPopen = hookExecutableImport("popen", (void *)cabinetPopen);
            *(void **)&realPclose = hookExecutableImport("pclose", (void *)cabinetPclose);
        }
        frameScaleInstall();
        if (current->sd2Rom)
            igsSd2Install(current, gameDir);
        else
            igsA27Install(current, gameDir);
        return 0;
    }

    *(void **)&realDlsym = hookExecutableImport("dlsym", (void *)gameDlsym);
    if (!realDlsym)
        log_warn("IGS: no dlsym import, GL is not loaded");
    *(void **)&realXOpenDisplay = hookExecutableImport("XOpenDisplay", (void *)gameXOpenDisplay);
    if (!realXOpenDisplay)
        log_warn("IGS: no XOpenDisplay import");
    detourFunction(current->biosCheck, biosCheck);
    for (int i = 0; i < 3; i++)
        if (current->keyboardReads[i])
            detourFunction(current->keyboardReads[i], noKey);
    if (MH_CreateHook((void *)(uintptr_t)current->setVideoMode, (void *)setVideoMode, (void **)&realSetVideoMode) != MH_OK)
        log_error("IGS: cannot hook the video mode");
    frameScaleInstall();
    igsPaletteInstall();
    igsIoInstall(current);
    return 0;
}
