#ifndef __i386__
#define __i386__
#endif
#undef __x86_64__
#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "../config/config.h"

#ifdef _WIN32
#include <windows.h>

extern HMODULE hGlDLL; 

GLADapiproc gladGlLoader(const char* name) {
    GLADapiproc proc = (GLADapiproc)GetProcAddress(hGlDLL, name);
    if (proc) return proc;

    typedef PROC(WINAPI* PFNWGLGETPROCADDRESS)(LPCSTR);
    PFNWGLGETPROCADDRESS wglGPA = (PFNWGLGETPROCADDRESS)GetProcAddress(hGlDLL, "wglGetProcAddress");
    
    if (wglGPA)
        return (GLADapiproc)wglGPA(name);

    return NULL;
}
#endif

bool gettingGPUVendor = true;

int getGPUVendorID()
{
    int vendorId;
    EmulatorConfig *config = getConfig();
    
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        exit(1);
    }

    uint32_t windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;

    SDL_Window *window = SDL_CreateWindow("OpenGL", 640, 480, windowFlags);
    if (!window)
    {
        fprintf(stderr, "Here Window could not be created! SDL_Error: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context)
    {
        fprintf(stderr, "OpenGL context could not be created! SDL_Error: %s\n", SDL_GetError());
        exit(1);
    }

#ifdef _WIN32
    if (!gladLoadGL((GLADloadfunc)gladGlLoader))
#else
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
#endif
    {
        fprintf(stderr, "Failed to initialize GLAD.\n");
        exit(EXIT_FAILURE);
    }

    config->GPUVendorString = strdup((char *)glad_glGetString(GL_VENDOR));
    
    if (!config->GPUVendorString)
    {
        fprintf(stderr, "Error: Could not get renderer string\n");
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        return ERROR_GPU;
    }
    gettingGPUVendor = false;
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);

    if (strstr(config->GPUVendorString, "NVIDIA") != NULL)
        vendorId = NVIDIA_GPU;
    else if (strstr(config->GPUVendorString, "AMD") != NULL)
        vendorId = AMD_GPU;
    else if (strstr(config->GPUVendorString, "Intel") != NULL)
        vendorId = INTEL_GPU;
    else if (strstr(config->GPUVendorString, "ATI") != NULL)
        vendorId = ATI_GPU;
    else if (strstr(config->GPUVendorString, "Error") != NULL)
        vendorId = ERROR_GPU;
    else
        vendorId = UNKNOWN_GPU;
    return vendorId;
}
