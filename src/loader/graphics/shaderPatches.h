#pragma once
#include <glad/gl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct
{
    const char *search;
    const char *replacement;
} SearchReplace;

#ifdef __cplusplus
extern "C"
{
#endif
    void gsEvoElfShaderPatcher();
    void gl_ProgramStringARB(int vertex_prg, int program_fmt, int program_len, char *program);
    void gl_ShaderSourceARB(uint32_t shaderObj, int count, const char **const string, const int *length);
    void *gl_XGetProcAddressARB(const unsigned char *procName);
    void gl_MultiTexCoord2fARB(uint32_t target, float s, float t);
    void gl_Color4ub(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
    void gl_Vertex3f(float x, float y, float z);
    void gl_TexCoord2f(float s, float t);
    int cg_GLIsProfileSupported(int profile);
    int cg_GLIsProfileSupportedATI(int profile);
    int cg_GLGetLatestProfile(int profileClass);
    void gl_ProgramParameters4fvNV(GLenum target, GLuint index, GLint count, const GLfloat *params);
    void gl_ProgramParameter4fvNV(GLenum target, GLuint index, const GLfloat *params);
    void gl_ProgramParameter4fNV(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
    GLboolean gl_IsProgramNV(GLuint program);
    void cacheModedShaderFiles();
    void srtvElfShaderPatcher();
    void gl_GenProgramsNV(GLuint n, GLuint *programs);
    void gl_BindProgramNV(uint32_t target, uint32_t id);
    void gl_LoadProgramNV(GLenum target, GLuint program, GLsizei len, char *string);
    void gl_DeleteProgramsNV(GLuint n, const GLuint *programs);
    void gl_EndOcclusionQueryNV(void);
    void gl_BeginOcclusionQueryNV(uint32_t id);
    void gl_BeginConditionalRenderNVX(GLuint id);
    void gl_GetOcclusionQueryuivNV(GLuint id, GLenum pname, GLuint *params);
    void gl_GenOcclusionQueriesNV(GLuint n, GLuint *ids);
    void glut_GameModeStringOR(const char *string);
    int compileWithCGC(char *command);
    void loadLibCg();
    void cacheNnstdshader();
    void vf5SetExposure(int var, float exposure);
    void vf5SetIntensity(int var, float *intensity);
    void vf5GetIdStart(void *iostream, int *idStart);
    void vf5SetDiffuse(int var, float r, float g, float b, float a);
    void hookVf5FSExposure(uint32_t expAddr, uint32_t intAddr, uint32_t idStartAddr, uint32_t difAddr);
    char *replaceInBlock(char *source, SearchReplace *searchReplace, int searchReplaceCount, char *startSearch, char *endSearch);
    char *replaceSubstring(const char *buffer, int start, int end, const char *search, const char *replace);

    void bridgeglEnable(GLenum cap);
    void bridgeglDisable(GLenum cap);

    char *bridgeCgGetProgramString(char *program, int e);
    char *bridgeCgCreateProgram(uint32_t context, int program_type, const char *program, int profile, const char *entry, const char **args);

    void bridgeglGenFencesNV(GLsizei n, GLuint *fences);
    void bridgeglDeleteFencesNV(GLsizei n, const GLuint *fences);
    void bridgeglSetFenceNV(GLuint fence, GLenum condition);
    GLboolean bridgeglTestFenceNV(GLuint fence);
    void bridgeglFinishFenceNV(GLuint fence);
    GLboolean bridgeglIsFenceNV(GLuint fence);

    void bridgeglBindTexture(GLenum target, GLuint texture);
    void bridgeglTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
    void bridgeglVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);

#ifdef __cplusplus
}
#endif