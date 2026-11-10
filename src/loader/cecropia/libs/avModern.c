// The FFmpeg side of the old API (see avBridge.h): today's libavformat,
// libavcodec and libswscale, linked in statically.

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>

#include "avBridge.h"

struct BridgeInput
{
    AVFormatContext *fmt;
    AVPacket *pkt;
};

struct BridgeDecoder
{
    AVCodecContext *ctx;
    AVPacket *pkt;
    AVFrame *frame;
};

struct BridgeFrame
{
    AVFrame *frame;
};

int bridgePacketPadding(void)
{
    return AV_INPUT_BUFFER_PADDING_SIZE;
}

BridgeInput *bridgeOpen(const char *path)
{
    BridgeInput *in = calloc(1, sizeof(*in));

    if (!in)
        return NULL;
    if (avformat_open_input(&in->fmt, path, NULL, NULL) < 0 || !(in->pkt = av_packet_alloc()))
    {
        avformat_close_input(&in->fmt);
        free(in);
        return NULL;
    }
    return in;
}

int bridgeFindStreamInfo(BridgeInput *in)
{
    return avformat_find_stream_info(in->fmt, NULL);
}

int bridgeStreamCount(BridgeInput *in)
{
    return (int)in->fmt->nb_streams;
}

void bridgeStreamInfo(BridgeInput *in, int index, BridgeStreamInfo *info)
{
    AVStream *st = in->fmt->streams[index];
    AVCodecParameters *par = st->codecpar;
    AVRational rate = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;

    memset(info, 0, sizeof(*info));
    info->type = par->codec_type == AVMEDIA_TYPE_VIDEO ? 0 : par->codec_type == AVMEDIA_TYPE_AUDIO ? 1
               : par->codec_type == AVMEDIA_TYPE_DATA  ? 2
                                                       : -1;
    info->codecId = par->codec_id;
    info->timeBaseNum = st->time_base.num;
    info->timeBaseDen = st->time_base.den;
    info->frameRateNum = rate.num;
    info->frameRateDen = rate.den;
    info->startTime = st->start_time;
    info->duration = st->duration;
    info->width = par->width;
    info->height = par->height;
    info->pixFmt = par->codec_type == AVMEDIA_TYPE_VIDEO ? par->format : -1;
    info->sampleRate = par->sample_rate;
    info->channels = par->ch_layout.nb_channels;
    info->bitRate = (int)par->bit_rate;
}

int bridgeReadPacket(BridgeInput *in, BridgePacket *out)
{
    int ret = av_read_frame(in->fmt, in->pkt);

    if (ret < 0)
        return ret;
    out->data = malloc((size_t)in->pkt->size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!out->data)
    {
        av_packet_unref(in->pkt);
        return AVERROR(ENOMEM);
    }
    memcpy(out->data, in->pkt->data, in->pkt->size);
    memset(out->data + in->pkt->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    out->size = in->pkt->size;
    out->streamIndex = in->pkt->stream_index;
    out->flags = in->pkt->flags & AV_PKT_FLAG_KEY;
    out->duration = (int)in->pkt->duration;
    out->pts = in->pkt->pts;
    out->dts = in->pkt->dts;
    out->pos = in->pkt->pos;
    av_packet_unref(in->pkt);
    return 0;
}

void bridgeClose(BridgeInput *in)
{
    if (!in)
        return;
    av_packet_free(&in->pkt);
    avformat_close_input(&in->fmt);
    free(in);
}

int bridgeHasDecoder(int codecId)
{
    return avcodec_find_decoder((enum AVCodecID)codecId) != NULL;
}

BridgeDecoder *bridgeDecoderOpen(BridgeInput *in, int streamIndex)
{
    AVStream *st = in->fmt->streams[streamIndex];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    BridgeDecoder *dec = calloc(1, sizeof(*dec));

    if (!codec || !dec)
    {
        free(dec);
        return NULL;
    }
    dec->ctx = avcodec_alloc_context3(codec);
    dec->pkt = av_packet_alloc();
    dec->frame = av_frame_alloc();
    if (!dec->ctx || !dec->pkt || !dec->frame || avcodec_parameters_to_context(dec->ctx, st->codecpar) < 0)
        goto fail;
    dec->ctx->pkt_timebase = st->time_base;
    // Slices only: frame threads would hold pictures back, which the game's
    // player (a picture per packet) does not expect.
    dec->ctx->thread_count = 0;
    dec->ctx->thread_type = FF_THREAD_SLICE;
    if (avcodec_open2(dec->ctx, codec, NULL) < 0)
        goto fail;
    return dec;

fail:
    bridgeDecoderClose(dec);
    return NULL;
}

void bridgeDecoderClose(BridgeDecoder *dec)
{
    if (!dec)
        return;
    av_frame_free(&dec->frame);
    av_packet_free(&dec->pkt);
    avcodec_free_context(&dec->ctx);
    free(dec);
}

BridgeFrame *bridgeFrameAlloc(void)
{
    BridgeFrame *f = calloc(1, sizeof(*f));

    if (f && !(f->frame = av_frame_alloc()))
    {
        free(f);
        return NULL;
    }
    return f;
}

void bridgeFrameFree(BridgeFrame *f)
{
    if (!f)
        return;
    av_frame_free(&f->frame);
    free(f);
}

void bridgeFramePicture(BridgeFrame *f, uint8_t **data, int *linesize, int *width, int *height, int *pixFmt,
                        int *keyFrame, int *pictType, int *repeatPict)
{
    for (int i = 0; i < 4; i++)
    {
        data[i] = f->frame->data[i];
        linesize[i] = f->frame->linesize[i];
    }
    *width = f->frame->width;
    *height = f->frame->height;
    *pixFmt = f->frame->format;
    *keyFrame = !!(f->frame->flags & AV_FRAME_FLAG_KEY);
    *pictType = f->frame->pict_type;
    *repeatPict = f->frame->repeat_pict;
}

static int sendPacket(BridgeDecoder *dec, const uint8_t *data, int size)
{
    av_packet_unref(dec->pkt);
    dec->pkt->data = (uint8_t *)data;
    dec->pkt->size = size;
    return avcodec_send_packet(dec->ctx, dec->pkt);
}

int bridgeDecodeVideo(BridgeDecoder *dec, BridgeFrame *f, const uint8_t *data, int size)
{
    int sent = sendPacket(dec, data, size), ret;

    if (sent < 0 && sent != AVERROR(EAGAIN))
        return sent;
    av_frame_unref(f->frame);
    ret = avcodec_receive_frame(dec->ctx, f->frame);
    // The decoder was full: its picture out, the packet in now.
    if (sent == AVERROR(EAGAIN))
        sendPacket(dec, data, size);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        return 0;
    return ret < 0 ? ret : 1;
}

static int sample16(const AVFrame *frame, int ch, int i)
{
    int planar = av_sample_fmt_is_planar(frame->format);
    int channels = frame->ch_layout.nb_channels;
    const uint8_t *base = frame->extended_data[planar ? ch : 0];
    int at = planar ? i : i * channels + ch;
    float f;

    switch (av_get_packed_sample_fmt(frame->format))
    {
    case AV_SAMPLE_FMT_U8:
        return (base[at] - 128) << 8;
    case AV_SAMPLE_FMT_S16:
        return ((const int16_t *)base)[at];
    case AV_SAMPLE_FMT_S32:
        return ((const int32_t *)base)[at] >> 16;
    case AV_SAMPLE_FMT_FLT:
        f = ((const float *)base)[at];
        break;
    case AV_SAMPLE_FMT_DBL:
        f = (float)((const double *)base)[at];
        break;
    default:
        return 0;
    }
    f *= 32768.0f;
    return f > 32767.0f ? 32767 : f < -32768.0f ? -32768 : (int)f;
}

int bridgeDecodeAudio(BridgeDecoder *dec, const uint8_t *data, int size, uint8_t **out, int *outSize)
{
    int ret = sendPacket(dec, data, size), used = 0, room = 0;
    uint8_t *buf = NULL;

    *out = NULL;
    *outSize = 0;
    if (ret < 0 && ret != AVERROR(EAGAIN))
        return ret;
    while ((ret = avcodec_receive_frame(dec->ctx, dec->frame)) >= 0)
    {
        int channels = dec->frame->ch_layout.nb_channels, n = dec->frame->nb_samples;
        int bytes = n * channels * 2;

        if (used + bytes > room)
        {
            uint8_t *grown = realloc(buf, (size_t)(used + bytes) * 2);
            if (!grown)
                break;
            buf = grown;
            room = (used + bytes) * 2;
        }
        int16_t *dst = (int16_t *)(buf + used);
        for (int i = 0; i < n; i++)
            for (int ch = 0; ch < channels; ch++)
                *dst++ = (int16_t)sample16(dec->frame, ch, i);
        used += bytes;
        av_frame_unref(dec->frame);
    }
    *out = buf;
    *outSize = used;
    return ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret >= 0 ? 0 : ret;
}

int bridgePixFmt(const char *name)
{
    return av_get_pix_fmt(name);
}

int bridgeConvert(uint8_t *const dstData[4], const int dstLinesize[4], int dstFmt, const uint8_t *const srcData[4],
                  const int srcLinesize[4], int srcFmt, int width, int height)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static struct SwsContext *sws;
    int ret;

    pthread_mutex_lock(&lock);
    sws = sws_getCachedContext(sws, width, height, srcFmt, width, height, dstFmt, SWS_BILINEAR, NULL, NULL, NULL);
    ret = sws ? sws_scale(sws, srcData, srcLinesize, 0, height, dstData, dstLinesize) : -1;
    pthread_mutex_unlock(&lock);
    return ret < 0 ? -1 : 0;
}

int64_t bridgeTime(void)
{
    return av_gettime();
}

void bridgeQuiet(void)
{
    av_log_set_level(AV_LOG_QUIET);
}
