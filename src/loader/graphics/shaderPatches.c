#ifndef __i386__
#define __i386__
#endif
#undef __x86_64__
#include <glad/gl.h>
#ifdef __linux__
#include <dlfcn.h>
#include <sys/mman.h>
#include <GL/glx.h>
#else
#include <SDL3/SDL_video.h>
#include <windows.h>
#endif
#include <fcntl.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "../elfLoader/glHooks.hpp"
#include "../redirections/filesystemShared.h"
#include "../redirections/libcShared.h"
#include "../config/config.h"
#include "../patching/flowControl.h"
#include "../log/log.h"
#include "shaderWork/common.h"

#include "shaderWork/abc.h"
#include "shaderWork/gsevo.h"
#include "shaderWork/hummer.h"
#include "shaderWork/id.h"
#include "shaderWork/lgj.h"
#include "shaderWork/mj4.h"
#include "shaderWork/or2.h"
#include "shaderWork/rtuned.h"
#include "shaderWork/srtv.h"
#include "shaderWork/vf5.h"

#define MAX_FILENAME_LENGTH 256

extern uint32_t gId;
extern int gGrp;
char tmpShader[150000];

int idxError = 0;
int idxVF5 = 0;
int vf5IdStart;
uint32_t vf5ExpOriAddr, vf5IntOriAddr, vf5IdStartOriAddr, vf5DifOriAddr;
char vf5StageNameAbbr[5];

typedef size_t *(*cgCreateContext_t)(void);
typedef void *(*cgCreateProgram_t)(size_t *context, int program_type, const char *program, int profile, const char *entry,
                                   const char **args);
typedef const char *(*cgGetProgramString_t)(char *program, int pname);
typedef void (*cgDestroyContext_t)(size_t *context);
typedef void (*cgDestroyProgram_t)(char *program);
typedef int (*cgGetError_t)();
typedef const char *(*cgGetErrorString_t)(int error);
typedef const char *(*cgGetLastErrorString_t)(int error);
typedef const char *(*cgGetLastListing_t)(size_t *context);

char *nnstdshader_vert;
char *nnstdshader_frag;

cgCreateContext_t _cgCreateContext;
cgCreateProgram_t _cgCreateProgram;
cgGetProgramString_t _cgGetProgramString;
cgDestroyContext_t _cgDestroyContext;
cgDestroyProgram_t _cgDestroyProgram;
cgGetError_t _cgGetError;
cgGetErrorString_t _cgGetErrorString;
cgGetLastErrorString_t _cgGetLastErrorString;
cgGetLastListing_t _cgGetLastListing;

/**
 *   Search and Replace function.
 */
char *replaceSubstring(const char *buffer, int start, int end, const char *search, const char *replace)
{
    int searchLen = strlen(search);
    int replaceLen = strlen(replace);
    int bufferLen = strlen(buffer);

    if (start < 0 || end > bufferLen || start > end)
    {
        printf("Invalid start or end positions\n");
        return NULL;
    }

    int maxOccurrences = 0;
    const char *tmp = buffer + start;
    while ((tmp = strstr(tmp, search)) && tmp < buffer + end)
    {
        maxOccurrences++;
        tmp += searchLen;
    }
    int maxNewLen =
        (bufferLen + (maxOccurrences * (replaceLen - searchLen))) + 15; // 15 instead of 1 because it crashes with SRTV for no reason??

    char *newBuffer = malloc(maxNewLen);
    memset(newBuffer, '\0', maxNewLen);

    if (!newBuffer)
    {
        printf("Memory allocation failed\n");
        return NULL;
    }

    const char *pos = buffer + start;
    const char *endPos = buffer + end;
    char *newPos = newBuffer;

    memcpy(newPos, buffer, start);
    newPos += start;

    while (pos < endPos)
    {
        const char *foundPos = strstr(pos, search);
        if (foundPos && foundPos < endPos)
        {
            int chunkLen = foundPos - pos;
            memcpy(newPos, pos, chunkLen);
            newPos += chunkLen;

            memcpy(newPos, replace, replaceLen);
            newPos += replaceLen;
            pos = foundPos + searchLen;
        }
        else
        {
            int remainingLen = endPos - pos;
            memcpy(newPos, pos, remainingLen);
            newPos += remainingLen;
            break;
        }
    }
    strcpy(newPos, buffer + end);

    return newBuffer;
}

char *replaceInBlock(char *source, SearchReplace *searchReplace, int searchReplaceCount, char *startSearch, char *endSearch)
{
    if (source == NULL || searchReplace == NULL)
        return NULL;

    int start_index = 0;
    if (startSearch != NULL && startSearch[0] != '\0')
    {
        char *start_pos = strstr(source, startSearch);
        if (start_pos == NULL)
            return source;
        start_index = start_pos - source + strlen(startSearch);
    }

    char *result = source;

    for (int i = 0; i < searchReplaceCount; i++)
    {
        const char *end_pos = result + strlen(result);

        if (endSearch[0] != '\0')
        {
            char *end_pos_candidate = strstr(result + start_index, endSearch);
            if (end_pos_candidate != NULL)
                end_pos = end_pos_candidate;
        }

        int end_index = end_pos - result;
        const char *target = searchReplace[i].search;
        const char *replacement = searchReplace[i].replacement;
        char *new_result = replaceSubstring(result, start_index, end_index, target, replacement);
        if (new_result != NULL)
        {
            if (result != source)
            {
                free(result);
            }
            result = new_result;
        }
    }
    return result;
}

/**
 *   glMultiTexCoord2fARB is replaced by glVertexAttrib2f to make it compatible
 * with Mesa.
 */
void gl_MultiTexCoord2fARB(uint32_t target, float s, float t)
{
    glad_glVertexAttrib2f((target - 33976), s, t);
}

/**
 *   glColor4ub is replaced by glVertexAttrib4Nub to make it compatible with
 * Mesa.
 */
void gl_Color4ub(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
    glad_glVertexAttrib4Nub(3, red, green, blue, alpha);
}

/**
 *   glVertex3f is replaced by glVertexAttrib3f to make it compatible with Mesa.
 */
void gl_Vertex3f(float x, float y, float z)
{
    glad_glVertexAttrib3f(0, x, y, z);
}

/**
 *   glTexCoord2f is replaced by glVertexAttrib2f to make it compatible with
 * Mesa.
 */
void gl_TexCoord2f(float s, float t)
{
    glad_glVertexAttrib2f(8, s, t);
}

/**
 *   glProgramParameters4fvNV is replaced by glProgramEnvParameters4fvEXT as
 * glProgramParameters4fvNV does not work or does not exist in modern systems.
 */
void gl_ProgramParameters4fvNV(GLenum target, GLuint index, GLsizei count, const GLfloat *v)
{
    glad_glProgramEnvParameters4fvEXT(target, index, count, v);
}

/**
 *   We force the cg compiler to use the profile we need to make the game work
 * on Mesa
 */
int cg_GLIsProfileSupported(int profile)
{
    switch (profile)
    {
        case 6146:
        case 6147:
        case 6148:
        case 6149:
        case 6151:
        case 7001:
        case 7010:
        case 7011:
        case 7012:
            return 0;
        case 6150:
        case 7000:
        case 7007:
        case 7008:
        case 7009:
            return 1;
    }
    return 0;
}

int cg_GLGetLatestProfile(int profileClass)
{
    int profile;

    if (profileClass == 8)
    {
        profile = 6150;
    }
    else if (profileClass == 9)
    {
        profile = 7000;
    }
    else
    {
        profile = 6145;
    }
    return profile;
}

/**
 *  Here we switch the program type from NV to ARB in Outrun
 */
#ifdef __linux__
#undef glEnable
void glEnable(GLenum cap)
{
    // void *(*_glEnable)(GLenum cap) = dlsym(RTLD_NEXT, "glEnable");
#else
void bridgeglEnable(GLenum cap)
{
#endif

    if ((cap == GL_FRAGMENT_PROGRAM_NV) && ((gId == OUTRUN_2_SP_SDX_SBMB) || (gId == OUTRUN_2_SP_SDX_SBMB_REVA)) &&
        (getConfig()->GPUVendor != NVIDIA_GPU))
    {
        cap = GL_FRAGMENT_PROGRAM_ARB;
    }
    glad_glEnable(cap);
}

/**
 *  Here we switch the program type from NV to ARB in Outrun
 */
#ifdef __linux__
#undef glDisable
void glDisable(GLenum cap)
{
    // void *(*_glDisable)(GLenum cap) = dlsym(RTLD_NEXT, "glDisable");
#else
void bridgeglDisable(GLenum cap)
{
#endif
    if ((cap == GL_FRAGMENT_PROGRAM_NV) && ((gId == OUTRUN_2_SP_SDX_SBMB) || (gId == OUTRUN_2_SP_SDX_SBMB_REVA)) &&
        (getConfig()->GPUVendor != NVIDIA_GPU))
    {
        cap = GL_FRAGMENT_PROGRAM_ARB;
    }
    glad_glDisable(cap);
}

/**
 *	The following 3 functions are replacements for SRTV and OR2
 */
void gl_EndOcclusionQueryNV(void)
{
    glad_glEndQueryARB(GL_SAMPLES_PASSED_ARB);
}

void gl_GetOcclusionQueryuivNV(GLuint id, GLenum pname, GLuint *params)
{
    glad_glGetQueryObjectuivARB(id, pname, params);
}

void gl_GenOcclusionQueriesNV(GLuint n, GLuint *ids)
{
    glad_glGenQueriesARB(n, ids);
}

void gl_BeginOcclusionQueryNV(GLuint id)
{
    glad_glBeginQueryARB(GL_SAMPLES_PASSED_ARB, id);
}

void gl_BeginConditionalRenderNVX(GLuint id)
{
    glad_glBeginConditionalRender(id, GL_QUERY_WAIT);
}

/**
 * This function patches the shaders inside the ELF in Ghost Squad EVO.
 */
void gsEvoElfShaderPatcher()
{
#ifdef __linux__
    int prot = mprotect((void *)0x820B000, 0x20000, PROT_EXEC | PROT_WRITE);
    if (prot != 0)
#else
    DWORD oldProtect;
    if (!VirtualProtect((void *)0x820B000, 0x20000, PAGE_EXECUTE_READWRITE, &oldProtect))
#endif
    {
        printf("Error: Cannot unprotect memory region to change variable\n");
        exit(1);
    }
    for (int i = 0; i < gsevoShaderOffsetsCount; i++)
    {
        char tmpProgram[5000];
        char *newProgram;
        memcpy(tmpProgram, (void *)(size_t)gsevoElfShaderOffsets[i].startOffset, gsevoElfShaderOffsets[i].sizeOfCode);
        memset((void *)(size_t)gsevoElfShaderOffsets[i].startOffset, '\0', gsevoElfShaderOffsets[i].totalSize);
        tmpProgram[gsevoElfShaderOffsets[i].sizeOfCode] = '\0';

        newProgram = replaceInBlock(tmpProgram, gsevoElf, gsevoElfCount, "", "");

        char *prevProgram = newProgram;
        newProgram = replaceInBlock(newProgram, gsevoElfMesa, gsevoElfCountMesa, "", "");
        if (prevProgram != tmpProgram && prevProgram != newProgram)
            free(prevProgram);

        prevProgram = newProgram;
        newProgram = replaceInBlock(newProgram, gsevoSet1, gsevoSetCount1, "", "");
        if (prevProgram != tmpProgram && prevProgram != newProgram)
            free(prevProgram);

        uint32_t newProgramLen = strlen(newProgram);

        newProgram[newProgramLen] = '\0';

        if (newProgramLen > gsevoElfShaderOffsets[i].totalSize)
        {
            printf("Error, size of modded program is bigger than the space reserved.");
            exit(1);
        }

        memcpy((void *)(size_t)gsevoElfShaderOffsets[i].startOffset, newProgram, newProgramLen);

        if (newProgram != tmpProgram)
            free(newProgram);
    }
#ifdef _WIN32
    VirtualProtect((void *)0x820B000, 0x20000, oldProtect, &oldProtect);
#endif
}

/**
 * This function cleans the text in the SRTV elf shaders
 * so they fit back in the elf after being patched.
 */
void cleanShaderString(char *str)
{
    char *src = str;
    char *dst = str;
    int spaceCount = 0;

    while (*src != '\0')
    {
        if (*src != '\t')
        {
            if (*src != ' ' || spaceCount == 0)
            {
                if ((*src == ' ' && *(src + 1) == ')') || (*src == ' ' && *(src + 1) == ',') || (*src == ' ' && *(src + 1) == '+') ||
                    (*src == ' ' && *(src + 1) == '='))
                    src++;
                *dst = *src;
                dst++;
                if ((*src == '(' && *(src + 1) == ' ') || (*src == ',' && *(src + 1) == ' ') || (*src == '+' && *(src + 1) == ' ') ||
                    (*src == '=' && *(src + 1) == ' '))
                    src++;
                spaceCount = (*src == ' ') ? spaceCount + 1 : 0;
            }
        }
        else
        {
            if (*(src + 1) != '\t' && (*(src + 1) > 32 || *(src + 1) < 126) && *(src + 1) != '=' && *(src + 1) != '\n')
            {
                *dst = *src;
                dst++;
            }
        }
        src++;
    }
    *dst = '\0';
}

/**
 * This function patches the shaders inside the ELF in Sega Race TV.
 */
void srtvElfShaderPatcher()
{
#ifdef __linux__
    int prot = mprotect((void *)0x854e000, 0x104000, PROT_EXEC | PROT_WRITE);
    if (prot != 0)
#else
    DWORD oldProtect;
    if (!VirtualProtect((void *)0x854e000, 0x104000, PAGE_EXECUTE_READWRITE, &oldProtect))
#endif
    {
        printf("Error: Cannot unprotect memory region to change variable\n");
        exit(1);
    }
    for (int i = 0; i < srtvShaderOffsetsCount; i++)
    {
        char tmpProgram[10000];
        char *newProgram;
        memcpy(tmpProgram, (void *)(size_t)srtvElfShaderOffsets[i].startOffset, srtvElfShaderOffsets[i].sizeOfCode);
        memset((void *)(size_t)srtvElfShaderOffsets[i].startOffset, '\0', srtvElfShaderOffsets[i].totalSize);
        tmpProgram[srtvElfShaderOffsets[i].sizeOfCode] = '\0';

        newProgram = replaceInBlock(tmpProgram, srtvElf, srtvElfCount, "", "");

        char firstWord[8];
        memcpy(firstWord, newProgram, 7);
        firstWord[7] = '\0';

        if (((strcmp(firstWord, "uniform") != 0) && (strcmp(firstWord, "void ma") != 0)) && (getConfig()->GPUVendor != NVIDIA_GPU))
        {
            memmove(newProgram + 13, newProgram, strlen(newProgram) + 1);
            memcpy(newProgram, "#version 120\n", 13);
        }

        cleanShaderString(newProgram);

        char *prevProgram = newProgram;
        newProgram = replaceInBlock(newProgram, srtvElf2, srtvElfCount2, "", "");
        if (prevProgram != tmpProgram && prevProgram != newProgram)
            free(prevProgram);

        int newProgramLen = strlen(newProgram);

        newProgram[newProgramLen] = '\0';

        memcpy((void *)(size_t)srtvElfShaderOffsets[i].startOffset, newProgram, newProgramLen);
        if (newProgram != tmpProgram)
            free(newProgram);
    }
#ifdef _WIN32
    VirtualProtect((void *)0x854e000, 0x104000, oldProtect, &oldProtect);
#endif
}

/**
 *   Here we intercept cgGetProgramString because some games like ID4 or ID5 we
 * force the shaders to be recompiled and there is a problem that the game feeds
 * some empty shaders.So we cache a good shader and then feed that shader to the
 * game instead of an empty one.
 */
#ifdef __linux__
char *cgGetProgramString(char *program, int e)
{
    char *(*_cgGetProgramString)(char *program, int e) = dlsym(RTLD_NEXT, "cgGetProgramString");
#else
void *real_cgGetProgramString = NULL;

char *bridgeCgGetProgramString(char *program, int e)
{
    char *(*_cgGetProgramString)(char *program, int e) = (char *(*)(char *, int))real_cgGetProgramString;
#endif

    if (gGrp != GROUP_ID4_EXP && gGrp != GROUP_ID4_JAP && gGrp != GROUP_ID5)
        return _cgGetProgramString(program, e);

    char *prgstr = _cgGetProgramString(program, e);

    if ((prgstr == NULL) || (*prgstr == '\0'))
    {
        return tmpShader;
    }
    else
    {
        if (sprintf(tmpShader, "%s", prgstr) < 0)
        {
            printf("Falied to save shader for next empty one.\n");
        }
    }
    return prgstr;
}

/**
 *   We intercept glProgramStringARB to do some shader patching before the
 *   shader gets loaded into the shader cache.
 */
void gl_ProgramStringARB(int target, int program_fmt, int program_len, char *program)
{
    char *newProgram = program;
    char *program_to_free = NULL;
    int newProgramLen;
    void *addr = __builtin_return_address(0);

    if (gId == GHOST_SQUAD_EVOLUTION_SBNJ)
    {
        if (addr == (void *)0x8179fab)
        {
            newProgram = replaceInBlock(program, gsevoSet1, gsevoSetCount1, "", "");
        }
    }
    else if (gGrp == GROUP_LGJ)
    {
        newProgram = replaceInBlock(program, lgjCompiledShadersPatch, lgjCompiledShadersPatchCount, "", "");
    }
    else if (gGrp == GROUP_ABC)
    {
        if (target == GL_VERTEX_PROGRAM_ARB)
        {
            newProgram = replaceInBlock(program, abcVsMesa, abcVsMesaCount, "", "");
        }
        else if (target == GL_FRAGMENT_PROGRAM_ARB)
        {
            newProgram = replaceInBlock(program, abcFsMesa, abcFsMesaCount, "", "");
        }
    }
    else if (gGrp == GROUP_OUTRUN)
    {
        if (target == GL_VERTEX_PROGRAM_ARB)
        {
            if (getConfig()->GPUVendor == NVIDIA_GPU)
                newProgram = program;
            else
                newProgram = replaceInBlock(program, orVsMesa, orVsMesaCount, "", "");
        }
        else if (target == GL_FRAGMENT_PROGRAM_ARB)
        {
            if (getConfig()->GPUVendor == NVIDIA_GPU)
                newProgram = replaceInBlock(program, orFsNvidia, orFsNvidiaCount, "", "");
            else
                newProgram = replaceInBlock(program, orFsMesa, orFsMesaCount, "", "");
        }
    }
    else if (gGrp == GROUP_VF5)
    {
        if (target == GL_VERTEX_PROGRAM_ARB)
        {
            if (getConfig()->GPUVendor == ATI_GPU)
            {
                newProgram = replaceInBlock(program, vf5VsAti, vf5VsAtiCount, "", "");
                if (gId == VIRTUA_FIGHTER_5_FINAL_SHOWDOWN_SBUV_REVA || gId == VIRTUA_FIGHTER_5_FINAL_SHOWDOWN_SBXX_REVB ||
                    gId == VIRTUA_FIGHTER_5_FINAL_SHOWDOWN_SBXX_REVB_6000)
                {
                    char *prevProgram = newProgram;
                    newProgram = replaceInBlock(newProgram, vf5VsAtifs, vf5VsAtifsCount, "", "");
                    if (prevProgram != program && prevProgram != newProgram)
                        free(prevProgram);
                }
            }
            else
            {
                newProgram = replaceInBlock(program, vf5VsMesa, vf5VsMesaCount, "", "");
            }
        }
        else if (target == GL_FRAGMENT_PROGRAM_ARB)
        {
            if (getConfig()->GPUVendor == ATI_GPU)
                newProgram = replaceInBlock(program, vf5FsAti, vf5FsAtiCount, "", "");
            else
                newProgram = replaceInBlock(program, vf5FsMesa, vf5FsMesaCount, "", "");
        }
    }
    else if (gId == R_TUNED_SBQW)
    {
        if (target == GL_VERTEX_PROGRAM_ARB)
        {
            newProgram = replaceInBlock(program, rtunedVsMesa, rtunedVsMesaCount, "", "");
        }
        else if (target == GL_FRAGMENT_PROGRAM_ARB)
        {
            newProgram = replaceInBlock(program, rtunedFsMesa, rtunedFsMesaCount, "", "");
        }
    }
    else if (gId == MJ4_SBPN_REVG || gId == MJ4_EVO_SBTA)
    {
        if (target == GL_VERTEX_PROGRAM_ARB)
        {
            newProgram = replaceInBlock(program, mj4VsMesa, mj4VsMesaCount, "", "");
        }
        else if (target == GL_FRAGMENT_PROGRAM_ARB)
        {
            newProgram = replaceInBlock(program, mj4FsMesa, mj4FsMesaCount, "", "");
        }
    }
    else if ((gGrp == GROUP_ID4_JAP || gGrp == GROUP_ID4_EXP || gGrp == GROUP_ID5) && getConfig()->GPUVendor == ATI_GPU)
    {
        if (strstr(program, "vertex.attrib[1]") != NULL)
            return;
        newProgram = replaceInBlock(program, idFsAti, idFsAtiCount, "", "");
    }

    if (newProgram != program)
        program_to_free = newProgram;

    newProgramLen = strlen(newProgram);

    glad_glProgramStringARB(target, program_fmt, newProgramLen, newProgram);

    if (gGrp == GROUP_VF5)
    {
        if (getConfig()->GPUVendor == ATI_GPU)
        {
            int err = glGetError();
            if (err == 1282)
            {
                char *newProgram2 = replaceInBlock(newProgram, vf5VsAti2, vf5VsAtiCount2, "", "");
                int newProgramLen2 = strlen(newProgram2);
                glad_glProgramStringARB(target, program_fmt, newProgramLen2, newProgram2);
                if (newProgram2 != newProgram)
                    free(newProgram2);
            }
        }
    }

    if ((getConfig()->showDebugMessages))
    {
        GLint result;
        result = glad_glGetError();
        if (result != GL_NO_ERROR)
        {
            fprintf(stderr, "Error: glProgramStringARB failed with error code %d.\n", result);
            //   Dump shader with error
            char filename[512] = {0};
            char path[256] = {0};
#ifdef __linux__
            int nchar = readlink("/proc/self/exe", path, 256 - 1);
#else
            DWORD nchar = GetModuleFileNameA(NULL, path, 256);
#endif
            if (nchar != 0)
                path[nchar] = '\0';
            else
                exit(1);
            char *dir_only = dirname(path);
            FILE *f;
            sprintf(filename, "%s/Error-%d.prg", "./", idxError);
            // sprintf(filename, "%s/%s", dir_only, fname);
            printf("Size: %d\n", newProgramLen);
            f = fopen(filename, "w+");
            fwrite(newProgram, sizeof(char), newProgramLen, f);
            fclose(f);
            idxError++;
        }
        GLint errorPos;
        glad_glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &errorPos);

        if (errorPos != -1)
        {
            const GLubyte *errorString = glad_glGetString(GL_PROGRAM_ERROR_STRING_ARB);
            if (errorString)
            {
                fprintf(stderr, "Program error at position %d: %s\n", errorPos, errorString);
            }
            else
            {
                fprintf(stderr, "Program error at position %d: Unknown error\n", errorPos);
            }
        }
    }
    if (program_to_free)
        free(program_to_free);
    return;
}

/**
 *   We use this to show error messages in the shader compilation.
 */
void gl_GetObjectParameterivARB(GLhandleARB obj, GLenum pname, GLint *params)
{
    glad_glGetObjectParameterivARB(obj, pname, params);

    if (getConfig()->showDebugMessages == 1)
    {
        GLchar infoLog[512];

        if ((int)*params <= 0)
        {
            glad_glGetShaderInfoLog(obj, 512, NULL, infoLog);
            printf("%s", infoLog);
        }
    }
}

/**
 *   We intercept cgCreateProgram to force the CG shader compiler to use
 *   different args depending on the shader being Vertex or Fragment.
 */
#ifdef __linux__
char *cgCreateProgram(uint32_t context, int program_type, const char *program, int profile, const char *entry, const char **args)
{
    char *(*_cgCreateProgram)(uint32_t context, int program_type, const char *program, int profile, const char *entry, const char **args) =
        dlsym(RTLD_NEXT, "cgCreateProgram");
#else
void *real_cgCreateProgram = NULL;

char *bridgeCgCreateProgram(uint32_t context, int program_type, const char *program, int profile, const char *entry, const char **args)
{
    char *(*_cgCreateProgram)(uint32_t context, int program_type, const char *program, int profile, const char *entry, const char **args) =
        (char *(*)(uint32_t, int, const char *, int, const char *, const char **))real_cgCreateProgram;
#endif

    if (getConfig()->GPUVendor == NVIDIA_GPU)
    {
        if ((gId != LETS_GO_JUNGLE_SBLU && gId != LETS_GO_JUNGLE_SBLU_REVA && gId != LETS_GO_JUNGLE_SBNR_SPECIAL) ||
            getConfig()->lgjRenderWithMesa == 0)
            return _cgCreateProgram(context, program_type, program, profile, entry, args);
    }

    if (gGrp == GROUP_ID4_EXP || gGrp == GROUP_ID4_JAP || gGrp == GROUP_ID5)
    {
        if ((profile == 6148) || (profile == 6150) || profile == 7001)
            profile = 6150;
        else if ((profile == 6149) || (profile == 6151) || profile == 7000)
            profile = 7000;
        else
            printf("profile %d not supported\n", profile);
    }
    const char *op6150[] = {"NumTemps=31", "MaxInstructions=1024", "MaxAddressRegs=1", "MaxLocalParams=256", NULL};

    const char *op7000[] = {"NumTemps=256",
                            "NumInstructionSlots=1447",
                            "MaxLocalParams=256",
                            "NumTexInstructionSlots=1447",
                            "NumMathInstructionSlots=1447",
                            "MaxTexIndirections=128",
                            "MaxDrawBuffers=8",
                            NULL};

    if (profile == 6150)
    {
        args = op6150;
    }
    else if (profile == 7000)
    {
        args = op7000;
    }
    return _cgCreateProgram(context, program_type, program, profile, entry, args);
}

void gl_ShaderSourceARB(GLhandleARB shaderObj, GLsizei count, const GLcharARB **const string, const GLint *length)
{

    if (gGrp != GROUP_HUMMER && gId != SEGA_RACE_TV_SBPF)
    {
        glad_glShaderSourceARB(shaderObj, count, string, length);
        return;
    }

    char *program_to_free = NULL;
    const GLcharARB *final_string = *string;
    GLint final_length = (length ? *length : 0);

    if (gId == SEGA_RACE_TV_SBPF)
    {
        char *newProgram = NULL;
        char tmp[250000];
        void *addr = __builtin_return_address(0);
        if (addr <= (void *)0x82fd823)
        {
            newProgram = strdup(*string);
        }
        else if (addr == (void *)0x84bbff1)
        {
            if (getConfig()->GPUVendor != NVIDIA_GPU)
            {
                sprintf(tmp, "#version 120\r\n%s", *string);
                newProgram = replaceInBlock(tmp, srtvVsMesa, srtvVsMesaCount, "", "");
            }
            else
            {
                sprintf(tmp, "%s", *string);
                newProgram = replaceInBlock(tmp, srtvVsNvidia, srtvVsNvidiaCount, "", "");
            }
        }
        else if (addr == (void *)0x084bc06b)
        {
            if (getConfig()->GPUVendor != NVIDIA_GPU)
            {
                sprintf(tmp, "#version 120\r\n%s", *string);
                newProgram = replaceInBlock(tmp, srtvFsMesa, srtvFsMesaCount, "", "");
            }
            else
            {
                newProgram = strdup(*string);
            }
        }

        if (newProgram)
        {
            final_string = newProgram;
            program_to_free = newProgram;
        }
    }
    else if (gGrp == GROUP_HUMMER)
    {
        void *addr = __builtin_return_address(0);
        if ((addr != (void *)0x830a501) && (addr != (void *)0x830a485) && (addr != (void *)0x08319841) && (addr != (void *)0x083197c5) &&
            (addr != (void *)0x83af939) && (addr != (void *)0x83af9b3) && (addr != (void *)0x83b0515) && (addr != (void *)0x83b058f))
        {
            final_string = strdup(hummerShaderFilesToMod[hummerExtremeShaderFileIndex].shaderBuffer);
            program_to_free = (char *)final_string;
        }
        else
        {
            glad_glShaderSourceARB(shaderObj, count, string, length);
            return;
        }
    }
    else
    {
        glad_glShaderSourceARB(shaderObj, count, string, length);
        return;
    }

    if (length != NULL)
    {
        final_length = strlen(final_string);
        glad_glShaderSourceARB(shaderObj, count, &final_string, &final_length);
    }
    else
    {
        glad_glShaderSourceARB(shaderObj, count, &final_string, NULL);
    }

    if (program_to_free)
    {
        free(program_to_free);
    }
}

/**
 * This function intercepts glXGetProcAddressARB
 * to point non existing functions to existing functions
 * or replacements in modern systems.
 */
void *gl_XGetProcAddressARB(const unsigned char *procName)
{
    int dbgMsg = getConfig()->showDebugMessages;
    if (gGrp != GROUP_HUMMER)
    {
        if (strcmp((const char *)procName, "glGenOcclusionQueriesNV") == 0)
        {
            if (dbgMsg)
                log_debug("%s Intercepted.\n", procName);

            return (void *)glad_glGenQueriesARB;
        }
        else if (strcmp((const char *)procName, "glDeleteOcclusionQueriesNV") == 0)
        {
            if (dbgMsg)
                log_debug("%s Intercepted.\n", procName);

            return (void *)glad_glDeleteQueriesARB;
        }
        else if (strcmp((const char *)procName, "glIsOcclusionQueryNV") == 0)
        {
            if (dbgMsg)
                log_debug("%s Intercepted.\n", procName);

            return (void *)glad_glIsQueryARB;
        }
        else if (strcmp((const char *)procName, "glBeginOcclusionQueryNV") == 0)
        {
            if (dbgMsg)
                log_debug("%s Intercepted.\n", procName);

            return (void *)gl_BeginOcclusionQueryNV;
        }
        else if (strcmp((const char *)procName, "glEndOcclusionQueryNV") == 0)
        {
            if (dbgMsg)
                log_debug("%s Intercepted.\n", procName);

            return (void *)gl_EndOcclusionQueryNV;
        }
        else if (strcmp((const char *)procName, "glGetOcclusionQueryivNV") == 0)
        {
            if (dbgMsg)
                log_debug("%s Intercepted.\n", procName);

            return (void *)glad_glGetQueryObjectuivARB;
        }
        else if (strcmp((const char *)procName, "glGetOcclusionQueryuivNV") == 0)
        {
            if (dbgMsg)
                log_debug("%s Intercepted.\n", procName);

            return (void *)glad_glGenQueriesARB;
        }
        else if (strcmp((const char *)procName, "glBeginConditionalRenderNVX") == 0)
        {
            if (dbgMsg)
                log_debug("%s Intercepted.\n", procName);

            return (void *)gl_BeginConditionalRenderNVX;
        }
        else if (strcmp((const char *)procName, "glEndConditionalRenderNVX") == 0)
        {
            if (dbgMsg)
                log_debug("%s Intercepted.\n", procName);

            return (void *)glad_glEndConditionalRender;
        }
        if (strstr((const char *)procName, "glProgramParameters4fvNV") != NULL)
        {
            if (dbgMsg)
                log_debug("%s Intercepted.\n", procName);

            if (getConfig()->GPUVendor == ATI_GPU && gGrp == GROUP_LGJ)
                return 0;
            return (void *)glad_glProgramEnvParameters4fvEXT;
        }
        else 
        if (strstr((const char *)procName, "glProgramEnvParameters4fv\0") != NULL && getConfig()->GPUVendor != ATI_GPU && getConfig()->GPUVendor != INTEL_GPU)
        {
            if (dbgMsg)
                log_debug("%s Intercepted.\n", procName);

            return (void *)glad_glProgramEnvParameters4fvEXT;
        }
        else if (strstr((const char *)procName, "glProgramLocalParameters4fv\0") != NULL && getConfig()->GPUVendor != ATI_GPU && getConfig()->GPUVendor != INTEL_GPU)
        {
            if (dbgMsg)
                log_debug("%s Intercepted.\n", procName);

            return (void *)glad_glProgramLocalParameters4fvEXT;
        }
        else if (strcmp((const char *)procName, "glGetObjectParameterivARB\0") == 0)
        {
            if (dbgMsg)
            {
                printf("glGetObjectParameterivARB Intercepted.\n");
            }
            return (void *)gl_GetObjectParameterivARB;
        }
    }
    if (strcmp((const char *)procName, "glShaderSourceARB\0") == 0)
    {
        if (dbgMsg)
        {
            printf("glShaderSourceARB Intercepted.\n");
        }
        return (void *)gl_ShaderSourceARB;
    }

#ifdef __linux__
    return glXGetProcAddressARB(procName);
#else
    return GLHooks_GetProcAddress((const char *)procName);
    // return SDL_GL_GetProcAddress(procName);
#endif
}

/**
 *   Functions for Outrun2
 */

void gl_GenProgramsNV(GLuint n, GLuint *programs)
{
    glad_glGenProgramsARB(n, programs);
}

void gl_BindProgramNV(GLenum target, GLuint id)
{
    if (target == GL_FRAGMENT_PROGRAM_NV)
    {
        target = GL_FRAGMENT_PROGRAM_ARB;
    }
    glad_glBindProgramARB(target, id);
}

void gl_LoadProgramNV(GLenum target, GLuint program, GLsizei len, char *string)
{
    gl_BindProgramNV(target, program);
    gl_ProgramStringARB(target, GL_PROGRAM_FORMAT_ASCII_ARB, len, string);
}

void gl_DeleteProgramsNV(GLuint n, const GLuint *programs)
{
    glad_glDeleteProgramsARB(n, programs);
}

void gl_ProgramParameter4fvNV(GLenum target, GLuint index, const GLfloat *params)
{
    glad_glProgramEnvParameter4fvARB(target, index, params);
}

void gl_ProgramParameter4fNV(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    glad_glProgramEnvParameter4fARB(target, index, x, y, z, w);
}

GLboolean gl_IsProgramNV(GLuint program)
{
    return glad_glIsProgramARB(program);
}

/**
 *   This function hook fixed the LOD (Level of Detail) for VF5 textures with Mesa
 */
#ifdef __linux__
#undef glBindTexture
void glBindTexture(GLenum target, GLuint texture)
{
    // void (*_glBindTexture)(GLenum target, GLuint texture) = dlsym(RTLD_NEXT, "glBindTexture");
    // _glBindTexture(target, texture);
#else
void bridgeglBindTexture(GLenum target, GLuint texture)
{
#endif
    if(!glad_glBindTexture)
        return;

    glad_glBindTexture(target, texture);
    if (gGrp == GROUP_VF5)
    {
        if (getConfig()->GPUVendor != NVIDIA_GPU)
        {
            glad_glTexParameterf(target, GL_TEXTURE_LOD_BIAS, -10.0f); // Adjust LOD bias
        }
    }
}

#ifdef __linux__
#undef glVertexPointer
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
#else
void bridgeglVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
#endif
{
    if (glad_glVertexPointer)
        glad_glVertexPointer(size, type, stride, pointer);
    if (getConfig()->GPUVendor != NVIDIA_GPU && glad_glVertexAttribPointer && glad_glEnableVertexAttribArray && gGrp == GROUP_OUTRUN)
    {
        glad_glVertexAttribPointer(0, size, type, GL_FALSE, stride, pointer);
        glad_glEnableVertexAttribArray(0);
    }
}

#ifdef __linux__
#undef glTexCoordPointer
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
#else
void bridgeglTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
#endif
{
    if (glad_glTexCoordPointer)
        glad_glTexCoordPointer(size, type, stride, pointer);
    if (getConfig()->GPUVendor != NVIDIA_GPU && glad_glVertexAttribPointer && glad_glEnableVertexAttribArray && gGrp == GROUP_OUTRUN)
    {
        glad_glVertexAttribPointer(8, size, type, GL_FALSE, stride, pointer);
        glad_glEnableVertexAttribArray(8);
    }
}

int parseCgcArgs(const char *input, const char ***compilerArgs, const char **outputFileName, char **bufferToFree)
{
    char *inputCopy = strdup(input);
    *bufferToFree = inputCopy;
    char *token = strtok(inputCopy, " ");
    char **args = malloc(256 * sizeof(char *));
    int count = 0;
    char *profile = NULL;

    while (token != NULL)
    {
        args[count++] = token;
        token = strtok(NULL, " ");
    }

    *compilerArgs = malloc((count * sizeof(char *)) + 4);
    (*compilerArgs)[0] = "-oglsl";
    (*compilerArgs)[1] = "-nowarn=7506,7529";
    (*compilerArgs)[2] = "-po";
    (*compilerArgs)[3] = "MaxLocalParams=256";
    int argIndex = 4;

    for (int i = 0; i < count; i++)
    {
        if (strcmp(args[i], "-o") == 0 && i + 1 < count)
        {
            *outputFileName = args[++i];
        }
        if (strcmp(args[i], "-profile") == 0 && i + 1 < count)
        {
            profile = args[++i];
        }
        else if (strncmp(args[i], "-D", 2) == 0 || strncmp(args[i], "-I", 2) == 0)
        {
            (*compilerArgs)[argIndex++] = args[i];
        }
    }
    (*compilerArgs)[argIndex] = NULL;
    free(args);

    if (strcmp(profile, "vp40") == 0)
    {
        return 6150;
    }
    else if (strcmp(profile, "fp40") == 0)
    {
        return 7000;
    }
    return 0;
}

char *findLibCg()
{
    const char *foundPath = NULL;
    FILE *libCgF = NULL;

    char appImageLib[MAX_PATH_LENGTH] = "";
    const char *appImageRoot = getenv("APP_IMG_ROOT");
    if (appImageRoot != NULL)
        snprintf(appImageLib, MAX_PATH_LENGTH, "%s/usr/lib32/libCg2.so", appImageRoot);

    char *pathsToCheck[] = {NULL, "/app/lib32/libCg2.so", appImageLib, NULL};

    if (strcmp(getConfig()->libCgPath, "") != 0)
    {
        pathsToCheck[0] = getConfig()->libCgPath;
    }

    const char *envPath = getenv("LINUX_LOADER_CURRENT_DIR");
    if (pathsToCheck[0] == NULL && envPath != NULL)
    {
        size_t pathLen = strlen(envPath) + strlen("/ll-deps/libCg2.so") + 1;
        pathsToCheck[0] = malloc(pathLen);
        snprintf(pathsToCheck[0], pathLen, "%s/ll-deps/libCg2.so", envPath);
        if (access(pathsToCheck[0], F_OK) != 0)
        {
            free(pathsToCheck[0]);
            pathsToCheck[0] = NULL;
        }
        if(pathsToCheck[0] == NULL)
        {
            pathLen = strlen(envPath) + strlen("/libCg2.so") + 1;
            pathsToCheck[0] = malloc(pathLen);
            snprintf(pathsToCheck[0], pathLen, "%s/libCg2.so", envPath);
            if (access(pathsToCheck[0], F_OK) != 0)
            {
                free(pathsToCheck[0]);
                pathsToCheck[0] = NULL;
            }
        }
    }

    if(pathsToCheck[0] == NULL) 
    {
        envPath = getenv("LINUX_LOADER_DEPS_PATH");
        if (envPath != NULL)
        {
            size_t pathLen = strlen(envPath) + strlen("/libCg2.so") + 1;
            pathsToCheck[0] = malloc(pathLen);
            snprintf(pathsToCheck[0], pathLen, "%s/libCg2.so", envPath);
            if(access(pathsToCheck[0], F_OK) != 0)
            {
                free(pathsToCheck[0]);
                pathsToCheck[0] = NULL;
            }
        }
    }

    for (int i = 0; i < 3; ++i)
    {
        if (pathsToCheck[i] == NULL)
            continue;

        if (access(pathsToCheck[i], F_OK) == 0)
        {
            foundPath = pathsToCheck[i];
            break;
        }
    }

    if (foundPath == NULL)
    {
        log_error("Error: Unable to find library libCg.so\n");
        log_error("Error: libCg.so version 2.0 is needed to compile the shaders in the game's folder.\n");
        exit(EXIT_FAILURE);
    }

    char libCgVersion[9] = {0};
    libCgF = fopen(foundPath, "rb");
    if (libCgF == NULL)
    {
        log_error("Failed to open: %s\n", foundPath);
        exit(EXIT_FAILURE);
    }

    fseek(libCgF, 0x226d84L, SEEK_SET);
    if (fread(libCgVersion, 1, sizeof(libCgVersion) - 1, libCgF) != sizeof(libCgVersion) - 1)
    {
        log_error("Failed to read version from: %s\n", foundPath);
        fclose(libCgF);
        exit(EXIT_FAILURE);
    }
    fclose(libCgF);

    if (strcmp(libCgVersion, "2.0.0.12") != 0)
    {
        log_error("ERROR: Wrong libCg.so version '%s', version 2.0.0.12 is needed.\n", libCgVersion);
        exit(EXIT_FAILURE);
    }

    char *resultPath = strdup(foundPath);
    if (resultPath == NULL)
    {
        log_error("Error allocating memory for result path");
        exit(EXIT_FAILURE);
    }

    return resultPath;
}

void loadLibCg()
{
#ifdef __linux__
    char *libCgFilePath = findLibCg();
    log_debug("Loading libCg.so from %s\n", libCgFilePath);
    void *handle = dlopen(libCgFilePath, RTLD_NOW);
#else
    HMODULE handle = LoadLibraryA("cg2.dll");
#endif
    if (!handle)
    {
#ifdef __linux__
        log_error("Error: Unable to find library libCg.so\n");
#else
        log_error("Error: Unable to find library cg.dll\n");
#endif
        log_error("Error: libCg.so version 2.0 is needed to compile the shaders in the game's folder.\n");
        exit(EXIT_FAILURE);
    }
#ifdef __linux__
    free(libCgFilePath);
    dlerror();
#endif

    char *error;
#ifdef __linux__
    _cgCreateContext = (cgCreateContext_t)dlsym(handle, "cgCreateContext");
#else
    _cgCreateContext = (cgCreateContext_t)(void *)GetProcAddress(handle, "cgCreateContext");
#endif
    error = sharedDlerror();
    if (error != NULL)
    {
        log_error("Error: Unable to find function cgCreateContext: %s\n", error);
        sharedDlclose(handle);
        exit(EXIT_FAILURE);
    }

    // Get the address of the cgCreateProgram function
#ifdef __linux__
    _cgCreateProgram = (cgCreateProgram_t)dlsym(handle, "cgCreateProgram");
#else
    _cgCreateProgram = (cgCreateProgram_t)(void *)GetProcAddress(handle, "cgCreateProgram");
#endif
    error = sharedDlerror();
    if (error != NULL)
    {
        log_error("Error: Unable to find function cgCreateProgram: %s\n", error);
        sharedDlclose(handle);
        exit(EXIT_FAILURE);
    }

    // Get the address of the cgGetProgramString function
#ifdef __linux__
    _cgGetProgramString = (cgGetProgramString_t)dlsym(handle, "cgGetProgramString");
#else
    _cgGetProgramString = (cgGetProgramString_t)(void *)GetProcAddress(handle, "cgGetProgramString");
#endif
    error = sharedDlerror();
    if (error != NULL)
    {
        log_error("Error locating cgGetProgramString: %s\n", error);
        sharedDlclose(handle);
        exit(EXIT_FAILURE);
    }

    // Get the address of the cgDestroyContext function
#ifdef __linux__
    _cgDestroyContext = (cgDestroyContext_t)dlsym(handle, "cgDestroyContext");
#else
    _cgDestroyContext = (cgDestroyContext_t)(void *)GetProcAddress(handle, "cgDestroyContext");
#endif
    error = sharedDlerror();
    if (error != NULL)
    {
        log_error("Error locating cgDestroyContext: %s\n", error);
        sharedDlclose(handle);
        exit(EXIT_FAILURE);
    }

    // Get the address of the cgDestroyProgram function
#ifdef __linux__
    _cgDestroyProgram = (cgDestroyProgram_t)dlsym(handle, "cgDestroyProgram");
#else
    _cgDestroyProgram = (cgDestroyProgram_t)(void *)GetProcAddress(handle, "cgDestroyProgram");
#endif
    error = sharedDlerror();
    if (error != NULL)
    {
        log_error("Error locating cgDestroyProgram: %s\n", error);
        sharedDlclose(handle);
        exit(EXIT_FAILURE);
    }

#ifdef __linux__
    _cgGetError = (cgGetError_t)dlsym(handle, "cgGetError");
#else
    _cgGetError = (cgGetError_t)(void *)GetProcAddress(handle, "cgGetError");
#endif
    if (!_cgGetError)
    {
        log_error("Error locating cgGetError\n");
        sharedDlclose(handle);
        exit(EXIT_FAILURE);
    }

#ifdef __linux__
    _cgGetErrorString = (cgGetErrorString_t)dlsym(handle, "cgGetErrorString");
#else
    _cgGetErrorString = (cgGetErrorString_t)(void *)GetProcAddress(handle, "cgGetErrorString");
#endif
    error = sharedDlerror();
    if (error != NULL)
    {
        log_error("Error locating cgGetErrorString: %s\n", error);
        sharedDlclose(handle);
        exit(EXIT_FAILURE);
    }

#ifdef __linux__
    _cgGetLastErrorString = (cgGetLastErrorString_t)dlsym(handle, "cgGetLastErrorString");
#else
    _cgGetLastErrorString = (cgGetLastErrorString_t)(void *)GetProcAddress(handle, "cgGetLastErrorString");
#endif
    error = sharedDlerror();
    if (error != NULL)
    {
        log_error("Error locating cgGetLastErrorString: %s\n", error);
        sharedDlclose(handle);
        exit(EXIT_FAILURE);
    }

#ifdef __linux__
    _cgGetLastListing = (cgGetLastListing_t)dlsym(handle, "cgGetLastListing");
#else
    _cgGetLastListing = (cgGetLastListing_t)(void *)GetProcAddress(handle, "cgGetLastListing");
#endif
    error = sharedDlerror();
    if (error != NULL)
    {
        log_error("Error locating cgGetLastListing: %s\n", error);
        sharedDlclose(handle);
        exit(EXIT_FAILURE);
    }
}

void cacheNnstdshader()
{
    int s;
    FILE *f;
    if (gId != QUIZ_AXA_SBMS && gId != QUIZ_AXA_SBUR_LIVE)
        f = sharedFopen("../fs/shader/nnstdshader.vert", "rb");
    else
        f = sharedFopen("../../data/shader/nnstdshader.vert", "rb");

    if (f == NULL)
    {
        printf("Error opening nnstdshader.vert\n");
        exit(1);
    }
    sharedFseek(f, 0, SEEK_END);
    s = sharedFtell(f);
    nnstdshader_vert = malloc(s + 1);
    memset(nnstdshader_vert, '\0', s + 1);
    sharedFseek(f, 0, SEEK_SET);
    sharedFread(nnstdshader_vert, 1, s, f);
    sharedFclose(f);

    if (gId != QUIZ_AXA_SBMS && gId != QUIZ_AXA_SBUR_LIVE)
        f = sharedFopen("../fs/shader/nnstdshader.frag", "rb");
    else
        f = sharedFopen("../../data/shader/nnstdshader.frag", "rb");

    if (f == NULL)
    {
        printf("Error opening nnstdshader.frag\n");
        exit(1);
    }
    sharedFseek(f, 0, SEEK_END);
    s = sharedFtell(f);
    nnstdshader_frag = malloc(s + 1);
    memset(nnstdshader_frag, '\0', s + 1);
    sharedFseek(f, 0, SEEK_SET);
    sharedFread(nnstdshader_frag, 1, s, f);
    sharedFclose(f);
}

int compileWithCGC(char *command)
{
    const char **compilerArgs = NULL;
    const char *outputFileName = NULL;
    char *argsBuffer = NULL;

    int profile = parseCgcArgs(command, &compilerArgs, &outputFileName, &argsBuffer);

    size_t *ctx = _cgCreateContext();

    char *program = NULL;

    if (profile == 6150)
    {
        program = _cgCreateProgram(ctx, 4112, nnstdshader_vert, profile, "main", compilerArgs);
        if (!program)
        {
            int err = _cgGetError();
            log_error("cgCreateProgram failed! Error code: %d\n", err);
            const char *errStr = _cgGetErrorString(err);
            if (errStr)
                log_error("  Error string: %s\n", errStr);
            log_error("\n--- Compiler Output ---\n");
            log_error("_cgGetLastListing function pointer: %p\n", (void *)_cgGetLastListing);
            fflush(stdout);
            if (_cgGetLastListing)
            {
                log_error("Calling cgGetLastListing...\n");
                fflush(stdout);
                const char *listing = _cgGetLastListing(ctx);
                log_error("cgGetLastListing returned: %p\n", (void *)listing);
                fflush(stdout);
                if (listing && listing[0] != '\0')
                {
                    log_error("%s\n", listing);
                }
                else
                {
                    log_error("(No compiler listing available)\n");
                }
            }
            else
            {
                log_error("cgGetLastListing not available\n");
            }
            log_error("--- End Compiler Output ---\n");

            _cgDestroyContext(ctx);
            free(nnstdshader_vert);
            exit(EXIT_FAILURE);
        }
    }
    else if (profile == 7000)
    {
        program = _cgCreateProgram(ctx, 4112, nnstdshader_frag, profile, "main", compilerArgs);
        int err = _cgGetError();
        if (err != 0)
        {
            int err = _cgGetError();
            log_error("cgCreateProgram failed! Error code: %d\n", err);
            const char *errStr = _cgGetErrorString(err);
            if (errStr)
                log_error("  Error string: %s\n", errStr);

            log_error("\n--- Compiler Output ---\n");
            log_error("_cgGetLastListing function pointer: %p\n", (void *)_cgGetLastListing);
            fflush(stdout);
            if (_cgGetLastListing)
            {
                log_error("Calling cgGetLastListing...\n");
                fflush(stdout);
                const char *listing = _cgGetLastListing(ctx);
                log_error("cgGetLastListing returned: %p\n", (void *)listing);
                fflush(stdout);
                if (listing && listing[0] != '\0')
                {
                    log_error("%s\n", listing);
                }
                else
                {
                    log_error("(No compiler listing available)\n");
                }
            }
            else
            {
                log_error("cgGetLastListing not available\n");
            }
            log_error("--- End Compiler Output ---\n");

            _cgDestroyContext(ctx);
            free(nnstdshader_vert);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        log_error("Error getting the profile.\n");
        exit(1);
    }

    const char *compiledSource = _cgGetProgramString(program, 4106);

    FILE *outputFile = fopen(outputFileName, "w+");
    if (outputFile == NULL)
    {
        log_error("Error creating output file %s", outputFileName);
        exit(1);
    }

    fprintf(outputFile, "%s", compiledSource);
    fclose(outputFile);
    free(argsBuffer);
    _cgDestroyProgram(program);
    _cgDestroyContext(ctx);
    return 0;
}

bool vf5PatchStage()
{
    bool patch = false;
    for (int x = 0; x < vf5StageListCount; x++)
    {
        if (strcmp(vf5StageNameAbbr, vf5StageList[x].stageAbb) == 0)
        {
            patch = vf5StageList[x].patch;
        }
    }
    return patch;
}

void vf5SetExposure(int var, float exposure)
{
    if (vf5PatchStage())
    {
        exposure *= 0.70;
    }
    (((void (*)(int, float))(size_t)vf5ExpOriAddr)(var, exposure));
}

void vf5SetIntensity(int var, float *intensity)
{
    if (vf5PatchStage())
    {
        intensity[0] *= 0.80;
        intensity[1] *= 0.80;
        intensity[2] *= 0.80;
    }
    (((void (*)(int, float *))(size_t)vf5IntOriAddr)(var, intensity));
}

void vf5GetIdStart(void *iostream, int *idStart)
{
    (((void (*)(void *, int *))(size_t)vf5IdStartOriAddr)(iostream, idStart));
    vf5IdStart = *idStart;
}

void vf5SetDiffuse(int var, float r, float g, float b, float a)
{
    if (vf5IdStart < 2 && vf5PatchStage())
    {
        r = r * 0.8;
        g = g * 0.8;
        b = b * 0.8;
        a = a * 0.8;
    }
    (((void (*)(int, float, float, float, float))(size_t)vf5DifOriAddr)(var, r, g, b, a));
}

void hookVf5FSExposure(uint32_t expAddr, uint32_t intAddr, uint32_t idStartAddr, uint32_t difAddr)
{
    vf5ExpOriAddr = (*(uint32_t *)(size_t)(expAddr + 1) + expAddr + 5);
    vf5IntOriAddr = (*(uint32_t *)(size_t)(intAddr + 1) + intAddr + 5);
    vf5IdStartOriAddr = (*(uint32_t *)(size_t)(idStartAddr + 1) + idStartAddr + 5);
    vf5DifOriAddr = (*(uint32_t *)(size_t)(difAddr + 1) + difAddr + 5);
    replaceCallAtAddress(expAddr, vf5SetExposure);
    replaceCallAtAddress(intAddr, vf5SetIntensity);
    replaceCallAtAddress(idStartAddr, vf5GetIdStart);
    replaceCallAtAddress(difAddr, vf5SetDiffuse);
}

// ABC glGenFencesNV and glDeleteFencesNV

static GLuint g_fenceCounter = 1;
static GLuint g_fenceMap[256];    // Maps fake fence ID -> index
static GLsync g_syncObjects[256]; // Stores actual GLsync objects
static int g_fenceCount = 0;

// Helper to find sync object by fake fence ID
static GLsync getSyncFromFence(GLuint fence)
{
    for (int i = 0; i < g_fenceCount; i++)
    {
        if (g_fenceMap[i] == fence)
            return g_syncObjects[i];
    }
    return NULL;
}

// Helper to remove fence from map
static void removeFence(GLuint fence)
{
    for (int i = 0; i < g_fenceCount; i++)
    {
        if (g_fenceMap[i] == fence)
        {
            // Remove from arrays
            g_fenceMap[i] = g_fenceMap[g_fenceCount - 1];
            g_syncObjects[i] = g_syncObjects[g_fenceCount - 1];
            g_fenceCount--;
            return;
        }
    }
}

// glGenFencesNV replacement for AMD/Intel
void bridgeglGenFencesNV(GLsizei n, GLuint *fences)
{
    if (getConfig()->GPUVendor != NVIDIA_GPU)
    {
        // Generate fake fence IDs (what the game expects)
        for (int i = 0; i < n; i++)
        {
            GLuint fakeFence = g_fenceCounter++;
            fences[i] = fakeFence;

            // Store mapping (sync object will be created in glSetFenceNV)
            if (g_fenceCount < 256)
            {
                g_fenceMap[g_fenceCount] = fakeFence;
                g_syncObjects[g_fenceCount] = 0; // null sync
                g_fenceCount++;
            }
        }
        return;
    }

    // NVIDIA - use original NV extension
    glad_glGenFencesNV(n, fences);
}

// glDeleteFencesNV replacement for AMD/Intel
void bridgeglDeleteFencesNV(GLsizei n, const GLuint *fences)
{
    if (getConfig()->GPUVendor != NVIDIA_GPU)
    {
        for (int i = 0; i < n; i++)
        {
            GLsync sync = getSyncFromFence(fences[i]);
            if (sync)
            {
                glad_glDeleteSync(sync);
                removeFence(fences[i]);
            }
        }
        return;
    }

    // NVIDIA - use original NV extension
    glad_glDeleteFencesNV(n, fences);
}

// glSetFenceNV replacement for AMD/Intel
void bridgeglSetFenceNV(GLuint fence, GLenum condition)
{
    if (getConfig()->GPUVendor != NVIDIA_GPU)
    {
        for (int i = 0; i < g_fenceCount; i++)
        {
            if (g_fenceMap[i] == fence)
            {
                if (g_syncObjects[i])
                    glad_glDeleteSync(g_syncObjects[i]);

                g_syncObjects[i] = glad_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                break;
            }
        }
        return;
    }

    // NVIDIA - use original NV extension
    glad_glSetFenceNV(fence, condition);
}

// glTestFenceNV replacement for AMD/Intel
GLboolean bridgeglTestFenceNV(GLuint fence)
{
    if (getConfig()->GPUVendor != NVIDIA_GPU)
    {
        GLsync sync = getSyncFromFence(fence);
        if (!sync)
            return GL_TRUE;

        GLenum status = glad_glClientWaitSync(sync, 0, 0);
        return (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) ? GL_TRUE : GL_FALSE;
    }

    // NVIDIA - use original NV extension
    return glad_glTestFenceNV(fence);
}

// glFinishFenceNV replacement for AMD/Intel
void bridgeglFinishFenceNV(GLuint fence)
{
    if (getConfig()->GPUVendor != NVIDIA_GPU)
    {
        GLsync sync = getSyncFromFence(fence);
        if (sync)
        {
            // Wait for GPU to complete (CPU blocking wait)
            glad_glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0xFFFFFFFFFFFFFFFFull);
        }
        return;
    }

    // NVIDIA - use original NV extension
    glad_glFinishFenceNV(fence);
}

// glIsFenceNV replacement for AMD/Intel
GLboolean bridgeglIsFenceNV(GLuint fence)
{
    if (getConfig()->GPUVendor != NVIDIA_GPU)
    {
        // Check if this fence ID exists in our map
        return getSyncFromFence(fence) != NULL ? GL_TRUE : GL_FALSE;
    }

    // NVIDIA - use original NV extension
    return glad_glIsFenceNV(fence);
}
