// libCgGL.so: the Cg/GL bridge of the Counter Strike NEO engine (engine_amd.so
// NEEDs it for its Cg shader path, NEO_CGFX_*). The engine's shaders need
// NVIDIA Cg to compile; on a non-NVIDIA GPU the GLSL fallback (cg_simple) is
// taken instead, so the bridge only has to be present and answer "the program
// is not loaded". The signatures below are the public Cg 1.0/1.1 API; the
// engine references every entry point through the PLT, so a no-op definition
// here satisfies the dynamic linker and the call sites.
//
// The return values are chosen to steer the engine's shader init down its
// GLSL fallback instead of the Cg path:
//   cgGLLoadProgram         -> 0   (the program failed to load)
//   cgGLIsProgramLoaded     -> 0   (the program is not loaded)
//   cgGLIsProfileSupported  -> 0   (the Cg profiles are not supported)
//   cgGLEnable*/Disable*    -> 0   (success; nothing to enable/disable)
//   everything else         -> void / 0
// The engine keeps its own GLSL fallback programs (the .fx files in czero/cgfx/
// compile to GLSL by a separate, non-Cg path), so the rendered output is the
// same, just without the Cg post-processes.

// Cg 1.0 core types (only what the signatures below need; the engine passes
// them as pointers, and the shim never dereferences them).
typedef void *CgglID;
typedef void *CgglProfile;
typedef void *CgglProgram;
typedef void *CgglTexture;
typedef void *CgglContext;

// cgGLBindProgram / cgGLUnbindProgram
void cgGLBindProgram(CgglProgram program)
{
}
void cgGLUnbindProgram(CgglProgram program)
{
}

// cgGLLoadProgram: 0 on failure (the engine then uses its GLSL fallback).
int cgGLLoadProgram(CgglProgram program)
{
    return 0;
}

// cgGLIsProgramLoaded: 0 (the program is not loaded).
int cgGLIsProgramLoaded(CgglProgram program)
{
    return 0;
}

// cgGLIsProfileSupported: 0 (the Cg profiles are not supported).
int cgGLIsProfileSupported(CgglProfile profile)
{
    return 0;
}

// cgGLEnableProfile / cgGLDisableProfile: 0 (success).
int cgGLEnableProfile(CgglProfile profile)
{
    return 0;
}
int cgGLDisableProfile(CgglProfile profile)
{
    return 0;
}

// cgGLEnableTextureParameter / cgGLDisableTextureParameter: 0 (success).
int cgGLEnableTextureParameter(CgglProgram program, int unit)
{
    return 0;
}
int cgGLDisableTextureParameter(CgglProgram program, int unit)
{
    return 0;
}

// cgGLSetTextureParameter: void.
void cgGLSetTextureParameter(CgglProgram program, CgglTexture texture)
{
}

// cgGLSetOptimalOptions: void.
void cgGLSetOptimalOptions(CgglProgram program)
{
}

// cgGLSetMatrixParameterfc: void.
void cgGLSetMatrixParameterfc(CgglProgram program, const char *name, int rows, int columns, const float *m)
{
}

// cgGLSetParameter1f / 1d / 2f / 2d / 3f / 3d / 4f / 4d: void.
void cgGLSetParameter1f(CgglProgram program, const char *name, float x)
{
}
void cgGLSetParameter1d(CgglProgram program, const char *name, double x)
{
}
void cgGLSetParameter2fv(CgglProgram program, const char *name, const float *v)
{
}
void cgGLSetParameter2dv(CgglProgram program, const char *name, const double *v)
{
}
void cgGLSetParameter3fv(CgglProgram program, const char *name, const float *v)
{
}
void cgGLSetParameter3dv(CgglProgram program, const char *name, const double *v)
{
}
void cgGLSetParameter4fv(CgglProgram program, const char *name, const float *v)
{
}
void cgGLSetParameter4dv(CgglProgram program, const char *name, const double *v)
{
}

// cgGLSetParameterArray1f / 1d / 2f / 2d / 3f / 3d / 4f / 4d: void.
void cgGLSetParameterArray1f(CgglProgram program, const char *name, int count, const float *v)
{
}
void cgGLSetParameterArray1d(CgglProgram program, const char *name, int count, const double *v)
{
}
void cgGLSetParameterArray2f(CgglProgram program, const char *name, int count, const float *v)
{
}
void cgGLSetParameterArray2d(CgglProgram program, const char *name, int count, const double *v)
{
}
void cgGLSetParameterArray3f(CgglProgram program, const char *name, int count, const float *v)
{
}
void cgGLSetParameterArray3d(CgglProgram program, const char *name, int count, const double *v)
{
}
void cgGLSetParameterArray4f(CgglProgram program, const char *name, int count, const float *v)
{
}
void cgGLSetParameterArray4d(CgglProgram program, const char *name, int count, const double *v)
{
}

// Version: the engine's Cg 1.0/1.1 bridge; report a current-enough version.
const char *cgGLGetVersion(void)
{
    return "1.5.11";
}
