// TMNT's dump contains OrenVid.dll, but the cabinet's Linux OrenVideo
// decoder is absent. Expose its import surface so optional attract videos
// fail cleanly instead of throwing a DllNotFoundException every frame.
#include <stdint.h>
#include <stddef.h>
#include <string.h>

uint64_t OrenVideo_Open(const char *filename, uint32_t flags)
{
    (void)filename;
    (void)flags;
    return 0;
}
void OrenVideo_Close(uint64_t video) { (void)video; }
uint32_t OrenVideo_GetInfo(uint64_t video, void *info)
{
    (void)video;
    if (info) memset(info, 0, 64);
    return 1;
}
uint32_t OrenVideo_GotoFrame(uint64_t video, uint32_t frame, uint32_t flags)
{
    (void)video; (void)frame; (void)flags;
    return 1;
}
uint32_t OrenVideo_DecompressVideoFrame(uint64_t video) { (void)video; return 1; }
uint32_t OrenVideo_DecompressVideoFrameMT(uint64_t video, uint32_t thread) { (void)video; (void)thread; return 1; }
uint32_t OrenVideo_DecompressAudioFrame(uint64_t video) { (void)video; return 1; }
uint32_t OrenVideo_SetAudioBufferSize(uint64_t video, uint16_t size) { (void)video; (void)size; return 1; }
uint32_t OrenVideo_DecompressFrameSlice(uint64_t video, uint32_t slice) { (void)video; (void)slice; return 1; }
uint32_t OrenVideo_DecompressFrameBegin(uint64_t video) { (void)video; return 1; }
uint32_t OrenVideo_DecompressFrameEnd(uint64_t video) { (void)video; return 1; }
uint32_t OrenVideo_MoveToTheNextFrame(uint64_t video) { (void)video; return 1; }
uint32_t OrenVideo_GetFrameInfo(uint64_t video, uint32_t slice, void *info)
{
    (void)video; (void)slice;
    if (info) memset(info, 0, 64);
    return 1;
}
uint32_t OrenVideo_GetAudioInfo(uint64_t video, void *info)
{
    (void)video;
    if (info) memset(info, 0, 64);
    return 1;
}
uint32_t OrenVideo_IsFrameReady(uint64_t video) { (void)video; return 0; }
uint32_t OrenVideo_SetPause(uint64_t video, uint8_t pause) { (void)video; (void)pause; return 0; }
int32_t OrenVideo_GetLastErrorCode(void) { return 1; }
uint32_t OrenVideo_UpdateTexture(uint64_t video, int32_t plane, void *texture, uint32_t width, uint32_t height,
                                 uint32_t pitch)
{
    (void)video; (void)plane; (void)texture; (void)width; (void)height; (void)pitch;
    return 1;
}
uint32_t OrenVideo_UpdateTextures(uint64_t video, void *y, void *cb, void *cr, void *alpha, uint32_t widthY,
                                  uint32_t heightY, uint32_t pitchY, uint32_t widthC, uint32_t heightC,
                                  uint32_t pitchC)
{
    (void)video; (void)y; (void)cb; (void)cr; (void)alpha; (void)widthY; (void)heightY;
    (void)pitchY; (void)widthC; (void)heightC; (void)pitchC;
    return 1;
}
void OrenVideo_SetUnityGraphicsDeviceType(int32_t type) { (void)type; }
void *OrenVideo_UpdateEventFunc(void) { return NULL; }
