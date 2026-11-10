#ifndef AV_BRIDGE_H
#define AV_BRIDGE_H

#include <stdint.h>

// The FFmpeg the old API (avOld.c) stands on, behind names of its own: the
// FFmpeg linked in keeps the old API's names (av_read_frame, av_free, ...)
// and is hidden in the library (see scripts/linux/build-theact-libs.sh).

typedef struct BridgeInput BridgeInput;
typedef struct BridgeDecoder BridgeDecoder;
typedef struct BridgeFrame BridgeFrame;

typedef struct
{
    int type;    // 0 video, 1 audio, 2 data, -1 other (AVMEDIA_TYPE_*)
    int codecId; // FFmpeg's AVCodecID: given back to bridgeDecoderOpen
    int timeBaseNum, timeBaseDen;
    int frameRateNum, frameRateDen;
    int64_t startTime, duration; // in time base units, INT64_MIN unknown
    int width, height, pixFmt;   // pixFmt: FFmpeg's AVPixelFormat
    int sampleRate, channels;
    int bitRate;
} BridgeStreamInfo;

typedef struct
{
    uint8_t *data; // malloc'd, bridgePacketPadding() zero bytes past size
    int size, streamIndex, flags, duration;
    int64_t pts, dts, pos;
} BridgePacket;

int bridgePacketPadding(void);

BridgeInput *bridgeOpen(const char *path);
int bridgeFindStreamInfo(BridgeInput *in);
int bridgeStreamCount(BridgeInput *in);
void bridgeStreamInfo(BridgeInput *in, int index, BridgeStreamInfo *info);
int bridgeReadPacket(BridgeInput *in, BridgePacket *pkt);
void bridgeClose(BridgeInput *in);

int bridgeHasDecoder(int codecId);
BridgeDecoder *bridgeDecoderOpen(BridgeInput *in, int streamIndex);
void bridgeDecoderClose(BridgeDecoder *dec);

BridgeFrame *bridgeFrameAlloc(void);
void bridgeFrameFree(BridgeFrame *frame);
// The decoded picture's planes (data, linesize: 4 each), size and format.
void bridgeFramePicture(BridgeFrame *frame, uint8_t **data, int *linesize, int *width, int *height, int *pixFmt,
                        int *keyFrame, int *pictType, int *repeatPict);

// A packet in (data, size: padded), at most a picture out: 1 if frame holds
// one, 0 if not, < 0 on an error.
int bridgeDecodeVideo(BridgeDecoder *dec, BridgeFrame *frame, const uint8_t *data, int size);
// A packet in, its samples out as interleaved signed 16 bits: their bytes
// in *out (malloc'd, *outSize bytes), < 0 on an error.
int bridgeDecodeAudio(BridgeDecoder *dec, const uint8_t *data, int size, uint8_t **out, int *outSize);

// AV_PIX_FMT_* the old API's formats are: -1 unknown.
int bridgePixFmt(const char *name);
// Picture conversion (scale to the same size), src's format given.
int bridgeConvert(uint8_t *const dstData[4], const int dstLinesize[4], int dstFmt, const uint8_t *const srcData[4],
                  const int srcLinesize[4], int srcFmt, int width, int height);

int64_t bridgeTime(void);
void bridgeQuiet(void);

#endif // AV_BRIDGE_H
