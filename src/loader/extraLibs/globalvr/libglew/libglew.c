// libGLEW.so.1.3 for Global VR games (Puck Off), which a PC has no 32-bit
// copy of: the GLEW entry points they import (their executables copy the
// variables, R_386_COPY: names and sizes must match), filled by glewInit
// from the GL driver.

#include <dlfcn.h>
#include <string.h>

typedef void (*GlewProc)(void);

#define GLEW_PROCS(X)                                                                                                  \
    X(ActiveTextureARB)                                                                                                \
    X(AttachObjectARB)                                                                                                 \
    X(BindAttribLocationARB)                                                                                           \
    X(ClientActiveTextureARB)                                                                                          \
    X(ColorTableEXT)                                                                                                   \
    X(CompileShaderARB)                                                                                                \
    X(CreateProgramObjectARB)                                                                                          \
    X(CreateShaderObjectARB)                                                                                           \
    X(DeleteObjectARB)                                                                                                 \
    X(DisableVertexAttribArrayARB)                                                                                     \
    X(EnableVertexAttribArrayARB)                                                                                      \
    X(GetAttribLocationARB)                                                                                            \
    X(GetInfoLogARB)                                                                                                   \
    X(GetObjectParameterivARB)                                                                                         \
    X(GetUniformLocationARB)                                                                                           \
    X(LinkProgramARB)                                                                                                  \
    X(LockArraysEXT)                                                                                                   \
    X(MultiTexCoord2fv)                                                                                                \
    X(ShaderSourceARB)                                                                                                 \
    X(UniformMatrix4fvARB)                                                                                             \
    X(UnlockArraysEXT)                                                                                                 \
    X(UseProgramObjectARB)                                                                                             \
    X(ValidateProgramARB)                                                                                              \
    X(VertexAttrib1sARB)                                                                                               \
    X(VertexAttrib3fvARB)                                                                                              \
    X(VertexAttrib4fvARB)                                                                                              \
    X(VertexAttrib4ivARB)                                                                                              \
    X(VertexAttribPointerARB)

#define DEFINE_PROC(name) GlewProc __glew##name;
GLEW_PROCS(DEFINE_PROC)

unsigned char __GLEW_ARB_vertex_shader;

#define GL_EXTENSIONS 0x1F03

// A whole name of the extension string.
static int hasExtension(const char *extensions, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = extensions; p && (p = strstr(p, name)); p += n)
        if ((p == extensions || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0'))
            return 1;
    return 0;
}

unsigned int glewInit(void)
{
    GlewProc (*getProcAddress)(const unsigned char *) = (GlewProc(*)(const unsigned char *))dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    const char *(*getString)(unsigned int) = (const char *(*)(unsigned int))dlsym(RTLD_DEFAULT, "glGetString");

    if (!getProcAddress || !getString)
        return 1; // GLEW_ERROR_NO_GL_VERSION
#define LOAD_PROC(name) __glew##name = getProcAddress((const unsigned char *)"gl" #name);
    GLEW_PROCS(LOAD_PROC)
    __GLEW_ARB_vertex_shader = hasExtension(getString(GL_EXTENSIONS), "GL_ARB_vertex_shader");
    return 0; // GLEW_OK
}
