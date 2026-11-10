// libavformat 50, libavcodec 51 and libavutil 49 (FFmpeg as of 2006), the
// part The Act's movie player (an ffplay of its day) calls, on today's
// FFmpeg (avModern.c, see avBridge.h).
//
// The game reads the structures' fields itself: they are laid out as the
// old headers had them, as far as the game reads them, with this side's
// own state past the end. The game copies packets by value and frees them
// with its packet's destruct then free(): av_malloc is malloc's.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "avBridge.h"

#define MAX_STREAMS 20
#define AUDIO_FRAME_MAX 192000 // AVCODEC_MAX_AUDIO_FRAME_SIZE

typedef struct
{
    int num, den;
} Rational;

typedef struct OldPacket
{
    int64_t pts, dts;
    uint8_t *data;
    int size;
    int streamIndex;
    int flags;
    int duration;
    void (*destruct)(struct OldPacket *);
    void *priv;
    int64_t pos;
} OldPacket;

typedef struct
{
    void *avClass;
    int bitRate;
    int bitRateTolerance, flags, subId, meMethod;
    void *extradata;
    int extradataSize;
    Rational timeBase;
    int width, height;
    int gopSize;
    int pixFmt;
    int rateEmu;
    void *drawHorizBand;
    int sampleRate, channels;
    int sampleFmt;
    int frameSize;
    uint8_t pad0[0xe0 - 0x50];
    int codecType;
    int codecId;
    // The game sets options in here (0xec, 0x100, 0x174, 0x180, 0x268).
    uint8_t pad1[0x400 - 0xe8];

    BridgeInput *input;
    int streamIndex;
    BridgeDecoder *decoder;
    uint8_t *pending; // decoded audio not handed out yet
    int pendingSize, pendingAt;
} OldCodecContext;

typedef struct
{
    int index;
    int id;
    OldCodecContext *codec;
    Rational rFrameRate;
    void *privData;
    int64_t codecInfoDuration;
    int codecInfoNbFrames;
    int64_t ptsVal, ptsNum, ptsDen;
    Rational timeBase;
    int ptsWrapBits;
    int streamCopy;
    int discard;
    float quality;
    int64_t startTime;
    int64_t duration;
    uint8_t pad[0x100 - 0x64];
} OldStream;

typedef struct
{
    uint8_t pad0[0x5c];
    int nbStreams;
    OldStream *streams[MAX_STREAMS];
    uint8_t pad1[0x1000 - 0xb0];

    BridgeInput *input;
} OldFormatContext;

typedef struct OldFrame
{
    uint8_t *data[4];
    int linesize[4];
    uint8_t *base[4];
    int keyFrame;
    int pictType;
    int64_t pts;
    uint8_t pad0[0x98 - 0x40];
    int repeatPict;
    uint8_t pad1[0x200 - 0x9c];

    BridgeFrame *frame;
    int pixFmt; // FFmpeg's, of the picture in data
    struct OldFrame *next;
} OldFrame;

_Static_assert(sizeof(OldPacket) == 0x34, "AVPacket");
_Static_assert(offsetof(OldPacket, destruct) == 0x24, "AVPacket.destruct");
_Static_assert(offsetof(OldCodecContext, timeBase) == 0x20, "AVCodecContext.time_base");
_Static_assert(offsetof(OldCodecContext, pixFmt) == 0x34, "AVCodecContext.pix_fmt");
_Static_assert(offsetof(OldCodecContext, sampleRate) == 0x40, "AVCodecContext.sample_rate");
_Static_assert(offsetof(OldCodecContext, codecType) == 0xe0, "AVCodecContext.codec_type");
_Static_assert(offsetof(OldStream, codec) == 0x08, "AVStream.codec");
_Static_assert(offsetof(OldStream, timeBase) == 0x3c, "AVStream.time_base");
_Static_assert(offsetof(OldStream, startTime) == 0x54, "AVStream.start_time");
_Static_assert(offsetof(OldFormatContext, nbStreams) == 0x5c, "AVFormatContext.nb_streams");
_Static_assert(offsetof(OldFormatContext, streams) == 0x60, "AVFormatContext.streams");
_Static_assert(offsetof(OldFrame, pts) == 0x38, "AVFrame.pts");
_Static_assert(offsetof(OldFrame, repeatPict) == 0x98, "AVFrame.repeat_pict");

// The old PixelFormat enum, by FFmpeg's names (RGBA32 is 0xAARRGGBB in a
// machine word: bgra here).
static const char *const oldPixFmtNames[] = {
    "yuv420p", "yuyv422", "rgb24", "bgr24", "yuv422p", "yuv444p", "bgra", "yuv410p", "yuv411p", "rgb565le",
    "rgb555le", "gray", "monow", "monob", "pal8", "yuvj420p", "yuvj422p", "yuvj444p", NULL, NULL, "uyvy422",
    "uyyvyy411",
};
#define OLD_PIX_FMTS (int)(sizeof(oldPixFmtNames) / sizeof(oldPixFmtNames[0]))

static int pixFmtFromOld(int old)
{
    return old >= 0 && old < OLD_PIX_FMTS && oldPixFmtNames[old] ? bridgePixFmt(oldPixFmtNames[old]) : -1;
}

static int pixFmtToOld(int fmt)
{
    for (int i = 0; i < OLD_PIX_FMTS; i++)
        if (oldPixFmtNames[i] && bridgePixFmt(oldPixFmtNames[i]) == fmt)
            return i;
    return -1;
}

// The frames avcodec_alloc_frame gave, for av_free and img_convert.
static pthread_mutex_t framesLock = PTHREAD_MUTEX_INITIALIZER;
static OldFrame *frames;

static OldFrame *findFrame(const void *p)
{
    OldFrame *f;

    pthread_mutex_lock(&framesLock);
    for (f = frames; f && f != p; f = f->next)
        ;
    pthread_mutex_unlock(&framesLock);
    return f;
}

static void fillStream(OldFormatContext *ic, int i)
{
    OldStream *st = ic->streams[i];
    OldCodecContext *c = st->codec;
    BridgeStreamInfo info;

    bridgeStreamInfo(ic->input, i, &info);
    st->index = i;
    st->id = i;
    st->rFrameRate.num = info.frameRateNum;
    st->rFrameRate.den = info.frameRateDen;
    st->timeBase.num = info.timeBaseNum;
    st->timeBase.den = info.timeBaseDen;
    st->ptsWrapBits = 64;
    st->startTime = info.startTime;
    st->duration = info.duration;
    c->codecType = info.type;
    c->codecId = info.codecId;
    c->bitRate = info.bitRate;
    c->width = info.width;
    c->height = info.height;
    c->pixFmt = pixFmtToOld(info.pixFmt);
    if (c->pixFmt < 0 && info.type == 0)
        c->pixFmt = 0; // converted from its real format anyway
    c->sampleRate = info.sampleRate;
    c->channels = info.channels;
    c->sampleFmt = 0; // SAMPLE_FMT_S16: what the decoding gives
    if (info.type == 0 && info.frameRateNum > 0)
    {
        c->timeBase.num = info.frameRateDen;
        c->timeBase.den = info.frameRateNum;
    }
    else if (info.type == 1 && info.sampleRate > 0)
    {
        c->timeBase.num = 1;
        c->timeBase.den = info.sampleRate;
    }
    c->input = ic->input;
    c->streamIndex = i;
}

static void packetDestruct(OldPacket *pkt)
{
    free(pkt->data);
    pkt->data = NULL;
    pkt->size = 0;
}

void av_register_all(void)
{
}

void *av_find_input_format(const char *name)
{
    (void)name;
    return NULL;
}

int av_open_input_file(OldFormatContext **icPtr, const char *filename, void *fmt, int bufSize, void *ap)
{
    OldFormatContext *ic;
    int n;

    (void)fmt;
    (void)bufSize;
    (void)ap;
    *icPtr = NULL;
    if (!(ic = calloc(1, sizeof(*ic))))
        return -12;
    if (!(ic->input = bridgeOpen(filename)))
    {
        free(ic);
        return -2;
    }
    n = bridgeStreamCount(ic->input);
    if (n > MAX_STREAMS)
        n = MAX_STREAMS;
    for (int i = 0; i < n; i++)
    {
        OldStream *st = calloc(1, sizeof(*st));
        OldCodecContext *c = calloc(1, sizeof(*c));
        if (!st || !c)
        {
            free(st);
            free(c);
            break;
        }
        st->codec = c;
        ic->streams[i] = st;
        ic->nbStreams = i + 1;
        fillStream(ic, i);
    }
    *icPtr = ic;
    return 0;
}

int av_find_stream_info(OldFormatContext *ic)
{
    int ret = bridgeFindStreamInfo(ic->input);

    for (int i = 0; i < ic->nbStreams; i++)
        fillStream(ic, i);
    return ret < 0 ? ret : 0;
}

int av_read_frame(OldFormatContext *ic, OldPacket *pkt)
{
    BridgePacket p;
    int ret = bridgeReadPacket(ic->input, &p);

    memset(pkt, 0, sizeof(*pkt));
    if (ret < 0)
        return ret;
    pkt->pts = p.pts;
    pkt->dts = p.dts;
    pkt->data = p.data;
    pkt->size = p.size;
    pkt->streamIndex = p.streamIndex;
    pkt->flags = p.flags;
    pkt->duration = p.duration;
    pkt->destruct = packetDestruct;
    pkt->pos = p.pos;
    return 0;
}

// Packets from av_read_frame own their data already.
int av_dup_packet(OldPacket *pkt)
{
    uint8_t *data;

    if (pkt->destruct == packetDestruct || !pkt->data)
        return 0;
    if (!(data = malloc((size_t)pkt->size + bridgePacketPadding())))
        return -12;
    memcpy(data, pkt->data, pkt->size);
    memset(data + pkt->size, 0, bridgePacketPadding());
    pkt->data = data;
    pkt->destruct = packetDestruct;
    return 0;
}

void av_close_input_file(OldFormatContext *ic)
{
    if (!ic)
        return;
    for (int i = 0; i < ic->nbStreams; i++)
    {
        if (ic->streams[i]->codec->decoder)
            bridgeDecoderClose(ic->streams[i]->codec->decoder);
        free(ic->streams[i]->codec->pending);
        free(ic->streams[i]->codec);
        free(ic->streams[i]);
    }
    bridgeClose(ic->input);
    free(ic);
}

// Any non-null codec: avcodec_open finds the stream's decoder itself.
void *avcodec_find_decoder(int id)
{
    return bridgeHasDecoder(id) ? (void *)(intptr_t)(id ? id : 1) : NULL;
}

int avcodec_open(OldCodecContext *c, void *codec)
{
    if (!codec || !c->input)
        return -1;
    if (c->decoder)
        return 0;
    return (c->decoder = bridgeDecoderOpen(c->input, c->streamIndex)) ? 0 : -1;
}

int avcodec_close(OldCodecContext *c)
{
    bridgeDecoderClose(c->decoder);
    c->decoder = NULL;
    free(c->pending);
    c->pending = NULL;
    c->pendingSize = c->pendingAt = 0;
    return 0;
}

// Threads are the decoder's own business (see avModern.c).
int avcodec_thread_init(OldCodecContext *c, int count)
{
    (void)c;
    (void)count;
    return 0;
}

void avcodec_thread_free(OldCodecContext *c)
{
    (void)c;
}

OldFrame *avcodec_alloc_frame(void)
{
    OldFrame *f = calloc(1, sizeof(*f));

    if (!f)
        return NULL;
    if (!(f->frame = bridgeFrameAlloc()))
    {
        free(f);
        return NULL;
    }
    f->pixFmt = -1;
    pthread_mutex_lock(&framesLock);
    f->next = frames;
    frames = f;
    pthread_mutex_unlock(&framesLock);
    return f;
}

int avcodec_decode_video(OldCodecContext *c, OldFrame *pic, int *gotPicture, uint8_t *buf, int size)
{
    int ret, width, height, pixFmt;

    *gotPicture = 0;
    if (!c->decoder)
        return -1;
    ret = bridgeDecodeVideo(c->decoder, pic->frame, buf, size);
    if (ret < 0)
        return ret;
    if (ret > 0)
    {
        bridgeFramePicture(pic->frame, pic->data, pic->linesize, &width, &height, &pixFmt, &pic->keyFrame,
                           &pic->pictType, &pic->repeatPict);
        pic->pixFmt = pixFmt;
        c->width = width;
        c->height = height;
        *gotPicture = 1;
    }
    return size;
}

// The game decodes a packet a call at a time, as long as some is left
// (buf, size): what one packet gives beyond AUDIO_FRAME_MAX is handed out on
// the next calls, which use none of it.
int avcodec_decode_audio(OldCodecContext *c, int16_t *samples, int *frameSize, uint8_t *buf, int size)
{
    int n;

    *frameSize = 0;
    if (!c->decoder)
        return -1;
    if (c->pendingAt >= c->pendingSize)
    {
        free(c->pending);
        c->pending = NULL;
        c->pendingSize = c->pendingAt = 0;
        if (bridgeDecodeAudio(c->decoder, buf, size, &c->pending, &c->pendingSize) < 0)
            return size; // skipped, as the old decoders' errors were
    }
    n = c->pendingSize - c->pendingAt;
    if (n > AUDIO_FRAME_MAX)
        n = AUDIO_FRAME_MAX;
    if (n > 0)
        memcpy(samples, c->pending + c->pendingAt, n);
    c->pendingAt += n;
    *frameSize = n;
    return c->pendingAt < c->pendingSize ? 0 : size;
}

int img_convert(OldFrame *dst, int dstPixFmt, const OldFrame *src, int srcPixFmt, int width, int height)
{
    const OldFrame *frame = findFrame(src);
    int srcFmt = frame && frame->pixFmt >= 0 ? frame->pixFmt : pixFmtFromOld(srcPixFmt);
    int dstFmt = pixFmtFromOld(dstPixFmt);

    if (srcFmt < 0 || dstFmt < 0)
        return -1;
    return bridgeConvert(dst->data, dst->linesize, dstFmt, (const uint8_t *const *)src->data, src->linesize, srcFmt,
                         width, height);
}

void *av_malloc(unsigned int size)
{
    void *p = NULL;

    return posix_memalign(&p, 16, size ? size : 1) == 0 ? p : NULL;
}

void av_free(void *p)
{
    OldFrame *f, **link;

    if (!p)
        return;
    pthread_mutex_lock(&framesLock);
    for (link = &frames; (f = *link) && f != p; link = &f->next)
        ;
    if (f)
        *link = f->next;
    pthread_mutex_unlock(&framesLock);
    if (f)
        bridgeFrameFree(f->frame);
    free(p);
}

void av_free_static(void)
{
}

int64_t av_gettime(void)
{
    return bridgeTime();
}

void av_log_set_level(int level)
{
    if (level < 0)
        bridgeQuiet();
}

unsigned avutil_version(void)
{
    return 49 << 16 | 0 << 8 | 0;
}

unsigned avcodec_version(void)
{
    return 51 << 16 | 25 << 8 | 0;
}

unsigned avformat_version(void)
{
    return 50 << 16 | 5 << 8 | 0;
}
