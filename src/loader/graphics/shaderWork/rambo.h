#include "common.h"

extern ShaderFilesToPatch ramboMesaShaderPatches[];
extern int ramboMesaShaderPatchesCount;
extern ShaderFilesToPatch ramboNvidiaShaderPatches[];
extern int ramboNvidiaShaderPatchesCount;

// Rewrites a skinning vertex program for Intel's Windows ARB compiler. Returns a malloc'd program, or NULL if unchanged.
char *ramboIntelVs(char *program);
