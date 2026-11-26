// IGS games (see igs.h): their cabinet's paths, the BIOS check and the
// start-up they need.

#include <execinfo.h>
#include <signal.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <glad/gl.h>

#include "igsBinder.h"
#include "igs.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"
#include "../graphics/glEntryRealign.h"
#include "../log/log.h"
#include <dlfcn.h>
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
    void *source = gpa((const unsigned char *)"glShaderSource");
    void *sourceArb = gpa((const unsigned char *)"glShaderSourceARB");
    // the loader's glXGetProcAddressARB hands out stack-realigning
    // trampolines (glEntryRealign.c): Scaleform keeps those, not libGL's
    void *names[] = {source, sourceArb, glRealignWrap(source), glRealignWrap(sourceArb)};
    void **slot = extensions;
    // its pointers, among the object's first words
    for (int i = 0; i < 512; i++)
        for (int n = 0; n < 4; n++)
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
    // (Scratch's lamp board, written only: there, its writes taken and its
    // status read 0)
    if (!strncmp(path, "/dev/ttyS", 9) && g->ttyZero)
        return "/dev/zero";
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
// A turned picture's frame (a36FrameTurn), the size the game asked for: 0,
// none (drawTurned).
static int turnedWidth, turnedHeight;
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
        if (!turnedWidth)
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
    if (current->a36FrameTurn && !getConfig()->rotateVertical)
    {
        turnedWidth = width;
        turnedHeight = height;
    }
    if (current->a36AsksScreen && current->frameWidth)
    {
        // (Demon Hunter asks for the screen's size, and draws its frame)
        width = current->frameWidth;
        height = current->frameHeight;
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

// A picture drawn for a monitor on its side (MuziBox): before each swap,
// the game's frame (the bottom left turnedWidth x turnedHeight of the back
// buffer) is copied to a texture and drawn back a quarter turn
// (a36FrameTurn 1: counterclockwise, -1: clockwise), fitted in the window
// (w x h), black bars aside. The game renders to framebuffers of its own
// through entry points the loader does not see: its framebuffer 0 stays
// the window's. The game's state put back after.
static void drawTurned(int w, int h)
{
    static GLuint texture;
    GLint draw = 0, read = 0, program = 0, activeTexture = GL_TEXTURE0, bound = 0;
    int pw, ph, ccw = current->a36FrameTurn > 0;
    // the turned picture, turnedHeight x turnedWidth, fitted in w x h
    if ((long)w * turnedWidth > (long)h * turnedHeight)
    {
        ph = h;
        pw = (int)((long)h * turnedHeight / turnedWidth);
    }
    else
    {
        pw = w;
        ph = (int)((long)w * turnedWidth / turnedHeight);
    }

    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glad_glPushAttrib(GL_ALL_ATTRIB_BITS);
    glad_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (glad_glUseProgram)
    {
        glad_glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glad_glUseProgram(0);
    }
    if (glad_glActiveTexture)
    {
        glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        for (int unit = 3; unit >= 0; unit--)
        {
            glad_glActiveTexture(GL_TEXTURE0 + unit);
            glad_glDisable(GL_TEXTURE_2D);
        }
    }
    if (GLAD_GL_ARB_vertex_program)
        glad_glDisable(GL_VERTEX_PROGRAM_ARB);
    if (GLAD_GL_ARB_fragment_program)
        glad_glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glad_glDisable(GL_TEXTURE_CUBE_MAP);
    glad_glDisable(GL_TEXTURE_3D);
    glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    if (!texture)
    {
        glad_glGenTextures(1, &texture);
        glad_glBindTexture(GL_TEXTURE_2D, texture);
        glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, turnedWidth, turnedHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else
        glad_glBindTexture(GL_TEXTURE_2D, texture);
    glad_glDrawBuffer(GL_BACK);
    glad_glReadBuffer(GL_BACK);
    glad_glDisable(GL_SCISSOR_TEST);
    glad_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glad_glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, turnedWidth, turnedHeight);
    glad_glViewport(0, 0, w, h);
    glad_glClearColor(0.f, 0.f, 0.f, 1.f);
    glad_glClear(GL_COLOR_BUFFER_BIT);
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glViewport((w - pw) / 2, (h - ph) / 2, pw, ph);
    glad_glDisable(GL_DEPTH_TEST);
    glad_glDisable(GL_STENCIL_TEST);
    glad_glDisable(GL_BLEND);
    glad_glDisable(GL_ALPHA_TEST);
    glad_glDisable(GL_CULL_FACE);
    glad_glDisable(GL_LIGHTING);
    glad_glDisable(GL_FOG);
    glad_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glad_glEnable(GL_TEXTURE_2D);
    glad_glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glad_glColor4f(1.f, 1.f, 1.f, 1.f);
    // the window's corners, from its bottom left counterclockwise, and the
    // frame's point each shows
    static const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    static const float ccwFrame[4][2] = {{1, 0}, {1, 1}, {0, 1}, {0, 0}};
    static const float cwFrame[4][2] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
    glad_glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++)
    {
        const float *t = ccw ? ccwFrame[i] : cwFrame[i];
        glad_glTexCoord2f(t[0], t[1]);
        glad_glVertex2f(corners[i][0], corners[i][1]);
    }
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
    glad_glPopAttrib();
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
}

// Hero of Robots' loading screen draws a frame after each step: with the
// swaps synced to the display, the loading takes minutes. Unsynced until the
// game starts its first scene (each scene's Init sends the board command 2
// through Game::iIgs036_AddSendCmd: igsGame.h's loadingEnds).
static int (*realAddSendCmd)(void *, int, void *, unsigned);
static volatile int loadingDone;
__attribute__((force_align_arg_pointer)) static int gameAddSendCmd(void *game, int command, void *data, unsigned size)
{
    if (command == 2 && !loadingDone)
    {
        loadingDone = 1;
        fprintf(stderr, "IGS: loading done, the swaps synced again\n");
    }
    return realAddSendCmd(game, command, data, size);
}

static void swapInterval(int interval)
{
    typedef void *(*GetProc)(const char *);
    GetProc getProc = (GetProc)dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    int (*mesa)(unsigned) = getProc ? (int (*)(unsigned))getProc("glXSwapIntervalMESA") : NULL;
    void (*ext)(void *, unsigned long, int) = getProc ? (void (*)(void *, unsigned long, int))getProc("glXSwapIntervalEXT") : NULL;
    void *(*display)(void) = (void *(*)(void))dlsym(RTLD_DEFAULT, "glXGetCurrentDisplay");
    unsigned long (*drawable)(void) = (unsigned long (*)(void))dlsym(RTLD_DEFAULT, "glXGetCurrentDrawable");
    // (NVIDIA's GLX has EXT only, but glvnd hands out a do-nothing MESA
    // entry point all the same: EXT first, MESA too for Mesa without it)
    if (ext && display && drawable && display() && drawable())
        ext(display(), drawable(), interval);
    if (mesa)
        mesa((unsigned)interval);
}

static void loadingSwaps(void)
{
    static int state; // 0, 1 unsynced, 2 synced again
    if (state == 0 && !loadingDone)
    {
        swapInterval(0);
        state = 1;
    }
    else if (state == 1 && loadingDone)
    {
        swapInterval(1);
        state = 2;
    }
}

// IGS_THROW (debugging): the game's C++ exceptions shown where thrown (its
// type's name, the callers), then thrown.
static void (*realCxaThrow)(void *, void *, void (*)(void *));

__attribute__((force_align_arg_pointer, noreturn)) static void gameCxaThrow(void *object, void *type,
                                                                          void (*destructor)(void *))
{
    void *frames[24];
    int n = backtrace(frames, 24);
    const char *name = type ? ((const char *const *)type)[1] : "?";

    fprintf(stderr, "IGS: C++ exception %s thrown, callers:", name);
    for (int i = 1; i < n; i++)
        fprintf(stderr, " %p", frames[i]);
    fprintf(stderr, "\n");
    realCxaThrow(object, type, destructor);
    __builtin_unreachable();
}

// and std::string's representation asked an impossible size (libstdc++
// then throws length_error from within): its caller shown
static void *(*realStringCreate)(size_t, size_t, const void *);

__attribute__((force_align_arg_pointer)) static void *gameStringCreate(size_t capacity, size_t old,
                                                                        const void *allocator)
{
    if (capacity > 0x3ffffffc)
    {
        void *frames[24];
        int n = backtrace(frames, 24);
        fprintf(stderr, "IGS: std::string of %zu bytes asked, callers:", capacity);
        for (int i = 1; i < n; i++)
            fprintf(stderr, " %p", frames[i]);
        fprintf(stderr, "\n");
    }
    return realStringCreate(capacity, old, allocator);
}

// and the abort that follows an uncaught one: where from
static void abortShown(int sig)
{
    void *frames[40];
    int n = backtrace(frames, 40);

    (void)sig;
    fprintf(stderr, "IGS: abort, callers:");
    for (int i = 1; i < n; i++)
        fprintf(stderr, " %p", frames[i]);
    fprintf(stderr, "\n");
    signal(SIGABRT, SIG_DFL);
}

// Ocean Party's seats' displays (on the cabinet, each seat's own LEDs; the
// screen shows none of it): a panel by each seat that has credits or bets,
// its credits and this round's bets, from the board's bookkeeping (its
// external SRAM: credits 0x500000c8 + 4 seat, bets 0x50003ef4 + 0xa0 seat).
static const uint8_t opFont[][7] = {
    // 5x7, rows top down, bit 4 the left column
    ['0' - '0'] = {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},   {14, 17, 1, 2, 4, 8, 31},
    {31, 2, 4, 2, 1, 17, 14},  {2, 6, 10, 18, 31, 2, 2},   {31, 16, 30, 1, 1, 17, 14},
    {6, 8, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},    {14, 17, 17, 14, 17, 17, 14},
    {14, 17, 17, 15, 1, 2, 12},
};
static const uint8_t opLetters[][8] = {
    {'P', 30, 17, 17, 30, 16, 16, 16}, {'C', 14, 17, 16, 16, 16, 17, 14}, {'R', 30, 17, 17, 30, 20, 18, 17},
    {'E', 31, 16, 16, 30, 16, 16, 31}, {'D', 28, 18, 17, 17, 17, 18, 28}, {'I', 14, 4, 4, 4, 4, 4, 14},
    {'T', 31, 4, 4, 4, 4, 4, 4},       {'B', 30, 17, 17, 30, 17, 17, 30},
};

static void opText(float x, float y, float pixel, const char *text)
{
    for (; *text; text++, x += 6 * pixel)
    {
        const uint8_t *rows = NULL;
        if (*text >= '0' && *text <= '9')
            rows = opFont[*text - '0'];
        for (size_t i = 0; !rows && i < sizeof(opLetters) / sizeof(opLetters[0]); i++)
            if (opLetters[i][0] == *text)
                rows = opLetters[i] + 1;
        if (!rows)
            continue;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (rows[r] >> (4 - c) & 1)
                    glad_glRectf(x + c * pixel, y + r * pixel, x + (c + 1) * pixel, y + (r + 1) * pixel);
    }
}

static void opOverlay(void)
{
    // where each seat's panel goes (fractions of the frame: its top left)
    static const float at[6][2] = {{0.10f, 0.80f}, {0.70f, 0.80f}, {0.80f, 0.45f},
                                   {0.70f, 0.13f}, {0.10f, 0.13f}, {0.02f, 0.45f}};
    GLint viewport[4];

    loadGl();
    if (!glad_glRectf || !glad_glPushAttrib)
        return;
    glad_glGetIntegerv(GL_VIEWPORT, viewport);
    float w = (float)viewport[2], h = (float)viewport[3], pixel = h / 270.0f;
    glad_glPushAttrib(GL_ALL_ATTRIB_BITS);
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glOrtho(0, w, h, 0, -1, 1);
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glDisable(GL_TEXTURE_2D);
    glad_glDisable(GL_DEPTH_TEST);
    glad_glDisable(GL_LIGHTING);
    glad_glDisable(GL_CULL_FACE);
    glad_glDisable(GL_SCISSOR_TEST);
    glad_glDisable(GL_STENCIL_TEST);
    glad_glDisable(GL_ALPHA_TEST);
    glad_glDisable(GL_FOG);
    glad_glEnable(GL_BLEND);
    glad_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (glad_glUseProgram)
        glad_glUseProgram(0);
    for (int seat = 0; seat < 6; seat++)
    {
        uint32_t credits = igsA36ExtSram(0x500000c8 + 4 * seat), bets = igsA36ExtSram(0x50003ef4 + 0xa0 * seat);
        char line[2][32];
        if (!credits && !bets)
            continue;
        snprintf(line[0], sizeof(line[0]), "%dP CREDIT %u", seat + 1, credits);
        snprintf(line[1], sizeof(line[1]), "BET %u", bets);
        float x = at[seat][0] * w, y = at[seat][1] * h;
        glad_glColor4f(0, 0, 0, 0.6f);
        glad_glRectf(x - 2 * pixel, y - 2 * pixel, x + (6 * 16 + 2) * pixel, y + 18 * pixel);
        glad_glColor4f(1, 0.9f, 0.3f, 1);
        opText(x, y, pixel, line[0]);
        glad_glColor4f(1, 1, 1, 1);
        opText(x, y + 9 * pixel, pixel, line[1]);
    }
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPopMatrix();
    glad_glPopAttrib();
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
    if (turnedWidth && glad_glCopyTexSubImage2D)
        drawTurned(screenWidth ? screenWidth : turnedWidth, screenWidth ? screenHeight : turnedHeight);
    else if (screenWidth)
        frameScalePresent(0, 0, screenWidth, screenHeight, getConfig()->borderEnabled,
                          getConfig()->whiteBorderPercentage, getConfig()->blackBorderPercentage);
    if (dir && now >= next && glad_glReadPixels)
    {
        char path[PATH_MAX];
        unsigned char *pixels;
        FILE *f;

        next = now + (getenv("IGS_SHOT_PERIOD") ? atof(getenv("IGS_SHOT_PERIOD")) : 4);
        glad_glGetIntegerv(GL_VIEWPORT, viewport);
        if (turnedWidth && !screenWidth)
        {
            viewport[0] = viewport[1] = 0;
            viewport[2] = turnedWidth;
            viewport[3] = turnedHeight;
        }
        else if (screenWidth)
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
    if (current && current->loadingEnds)
        loadingSwaps();
    if (current && current->casCard)
    {
        loadGl();
        igsBinderDraw(); // the card binder, in the game's frame (before its stretching)
    }
    if (current && current->a36Rom && current->frameWidth)
        stretchFrame();
    if (current && (current->a36Rom || current->sr1Rom))
        frameDone();
}

static void (*realSdlSwapBuffers)(void);

__attribute__((force_align_arg_pointer)) static void sdlSwapBuffers(void)
{
    if (current && current->a36FishBoard == 4)
        opOverlay();
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
// MuziBox's online service (IGS's server, long gone): its connections out
// of the machine refused at once, not left to time out (seconds each, in
// its loading and attract). The local ones go through.
static int (*realConnect)(int, const struct sockaddr *, socklen_t);

__attribute__((force_align_arg_pointer)) static int gameConnect(int fd, const struct sockaddr *address, socklen_t length)
{
    if (address && address->sa_family == AF_INET &&
        (ntohl(((const struct sockaddr_in *)address)->sin_addr.s_addr) >> 24) != 127)
    {
        errno = ECONNREFUSED;
        return -1;
    }
    return realConnect(fd, address, length);
}

// Rock Fever 5 opens some of its files as ../linux/main/... (its cabinet's
// directory, /linux): here its own directory, the working one.
static const char *gamePath(const char *path)
{
    return path && !strncmp(path, "../linux/", 9) ? path + 9 : path;
}

__attribute__((force_align_arg_pointer)) static FILE *gameFopen(const char *path, const char *mode)
{
    return fopen(gamePath(path), mode);
}

__attribute__((force_align_arg_pointer)) static int gameOpen(const char *path, int flags, ...)
{
    va_list args;
    va_start(args, flags);
    mode_t mode = (flags & O_CREAT) ? va_arg(args, mode_t) : 0;
    va_end(args);
    return open(gamePath(path), flags, mode);
}

__attribute__((force_align_arg_pointer)) static int gameAccess(const char *path, int mode)
{
    return access(gamePath(path), mode);
}

// A function of the game's made to return 0 (returnZero).
__attribute__((force_align_arg_pointer)) static int returnZero(void)
{
    return 0;
}

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

    if (current->a36Rom && current->a36PlxOpen)
    {
        // The 2008 cabinets (Percussion Master 2008, Rock Fever 5): the
        // IGS036 board behind a PLX9030, SDL 1.2 linked in (its video mode
        // at setVideoMode, 0: as the game asks), GL imported.
        hookExecutableImport("system", (void *)gameSystem);
        hookExecutableImport("fopen", (void *)gameFopen);
        hookExecutableImport("fopen64", (void *)gameFopen);
        hookExecutableImport("open", (void *)gameOpen);
        hookExecutableImport("open64", (void *)gameOpen);
        hookExecutableImport("access", (void *)gameAccess);
        // (its SDL's glX calls through dlsym: the swap the loader's)
        *(void **)&realDlsym = hookExecutableImport("dlsym", (void *)gameDlsym);
        // (and its own Xlib extensions: Gold Captain's lock, below)
        *(void **)&realXOpenDisplay = hookExecutableImport("XOpenDisplay", (void *)gameXOpenDisplay);
        if (current->setVideoMode &&
            MH_CreateHook((void *)(uintptr_t)current->setVideoMode, (void *)setVideoMode, (void **)&realSetVideoMode) != MH_OK)
            log_error("IGS: cannot hook the video mode");
        frameScaleInstall();
        igsA36Install(current, gameDir);
        return 0;
    }

    if (current->a36Rom && current->a36Sdl)
    {
        // The A36 board's SDL games (Fish Hunter SKL 2, MuziBox): SDL 1.2
        // imported (see the A27's below), Fish Hunter's HASP dongle.
        setenv("SDL12COMPAT_OPENGL_SCALING", "0", 1);
        *(void **)&realSetVideoMode = hookExecutableImport("SDL_SetVideoMode", (void *)importedSetVideoMode);
        *(void **)&realSdlSwapBuffers = hookExecutableImport("SDL_GL_SwapBuffers", (void *)sdlSwapBuffers);
        if (!realSetVideoMode || !realSdlSwapBuffers)
            log_error("IGS: no SDL_SetVideoMode or SDL_GL_SwapBuffers import");
        hookExecutableImport("system", (void *)gameSystem);
        frameScaleInstall();
        if (current->a36Offline)
            *(void **)&realConnect = hookExecutableImport("connect", (void *)gameConnect);
        if (current->haspApi[IGS_HASP_LOGIN] || current->soundLoad || current->haspFile)
            igsHaspInstall(current); // (the dongle's API, the sound loader's fix)
        if (current->returnZero)
            detourFunction(current->returnZero, (void *)returnZero);
        for (int i = 0; i < 2 && current->glesPrecision[i]; i++)
        {
            const char *statement = (const char *)(uintptr_t)current->glesPrecision[i];
            const char *end = strchr(statement, ';');
            char spaces[129] = "";
            size_t length = end ? (size_t)(end - statement) + 1 : 0;
            if (strncmp(statement, "precision", 9) || length * 2 >= sizeof(spaces))
            {
                log_error("IGS: no GL ES precision statement at %08x", current->glesPrecision[i]);
                continue;
            }
            for (size_t k = 0; k < length; k++)
                memcpy(spaces + 2 * k, "20", 3);
            patchMemoryFromString(current->glesPrecision[i], spaces);
        }
        igsA36Install(current, gameDir);
        return 0;
    }

    if (current->a36Rom)
    {
        if (current->ogrePlugins)
            ogrePluginsPrepare();
        hookExecutableImport("system", (void *)gameSystem);
        *(void **)&realPopen = hookExecutableImport("popen", (void *)cabinetPopen);
        *(void **)&realPclose = hookExecutableImport("pclose", (void *)cabinetPclose);
        if (current->haspFile)
            igsHaspInstall(current); // (Crazy Shoot's dongle, in its plugin: igsA36.c's a36Library)
        if (current->loadingEnds && MH_CreateHook((void *)(uintptr_t)current->loadingEnds, (void *)gameAddSendCmd,
                                                  (void **)&realAddSendCmd) != MH_OK)
            log_error("IGS: cannot hook the game's board commands");
        igsA36Install(current, gameDir);
        if (current->casCard)
        {
            igsCardInstall(current);
            igsBinderInstall();
        }
        if (getenv("IGS_THROW"))
        {
            *(void **)&realCxaThrow = hookExecutableImport("__cxa_throw", (void *)gameCxaThrow);
            *(void **)&realStringCreate = hookExecutableImport("_ZNSs4_Rep9_S_createEjjRKSaIcE",
                                                              (void *)gameStringCreate);
            signal(SIGABRT, abortShown);
        }
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
