// Gaelco's sound module (see gaelco.h): sndgaelco.o's sampler.
//
// The game hands the module its sample ROM (roms.new: a directory of sample
// headers, then 8-bit, 16-bit and IMA ADPCM samples) with
// iogaelco_dump_sound_buffer, then commands (set a channel up with a
// sample, start it, its pitch, panning, volume ramp, key off, stop) with
// iogaelco_add_comm, which it validates in a batch
// (IOINTF_CMD_FLUSHCOMMBUF). The module ran them at each of the AC97
// codec's buffer interrupts and mixed the channels into its next buffer:
// 256 frames of four channels (front and rear pairs) at 48 kHz. Here the
// same sampler (Gaelco's sndio.c, whose source is in Tuning Race's dump)
// fills an SDL3 stream, its four channels folded into two.

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gaelco.h"
#include "../log/log.h"

#define RATE 48000
#define FRAMES_PER_BUFFER 256 // the module's AC97 buffer: 0x800 bytes, 4 x 16 bits
#define NUMCAN 32             // the channel masks are 32 bits
#define COMBUF_SIZE 0x80
#define COMBUF_MASK 0x7f

// The interface commands (the game's iocommands.h).
#define IOINTF_CMD_MASTERVOL 0
#define IOINTF_CMD_MASTERBALANCE 1
#define IOINTF_CMD_GETCHANNELSTATUS 2
#define IOINTF_CMD_SOUNDSTART 3
#define IOINTF_CMD_FLUSHCOMMBUF 4
#define IOINTF_CMD_BUFFERREADY 13
#define IOINTF_CMD_BUFFERREADY_2003 11 // Tokyo Cop's module (see gaelcoIo.c)

// Sample header, in the ROM's directory.
typedef struct
{
    uint32_t start; // offset of the sample in the ROM
    uint32_t ns;    // its length in samples
    uint32_t nStartLoop, nEndLoop;
    uint16_t phinc; // 0x1000: the codec's rate
    int16_t vnorm, rkon, rkoff;
    int16_t ramping;
    int16_t flags;
    int32_t pad1;
} SHeader;

#define ST_MASK 0x6
#define ST_8BIT 0
#define ST_16BIT 2
#define ST_ADPCM 4

#define SC_SETUP 1
#define SC_START 2
#define SC_FREQ 3
#define SC_PAN 4
#define SC_VOL 5
#define SC_KOFF 6
#define SC_STOP 7

typedef struct Sample
{
    uint32_t pSample; // offset in the ROM
    int (*step)(struct Sample *ps);
    uint32_t phaseLO, phaseHI; // phase * 0x1000
    uint32_t phEndLO, phEndHI; // length * 0x1000
    uint32_t ssize;            // size in bytes
    uint8_t aindex;            // ADPCM step index
    uint8_t flags;
    int16_t data0, data1;
    int16_t vmaster; // volumes normalized to 0x1000
    uint16_t phinc;
    int16_t vfin, ramping;
    int16_t nsample;
    uint16_t vpan[4];
    const uint8_t *hfile;
} Sample;

#define ENABLE 1
#define LOOP 8
#define KEYOFF 0x10
#define ADV 0x40

typedef struct
{
    int16_t command, channel;
    uint16_t parms[4];
} SoundComm;

static const int16_t indexTable[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static const int16_t stepsizeTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static uint8_t *samplesBuffer;
static uint32_t samplesSize;
static const SHeader *dirSound;
static int soundDirSize;

static Sample samples[NUMCAN];
static volatile uint32_t soundChannels;
static int vglobal, vbalance[4];
static int nsframes;

static SoundComm combuf[COMBUF_SIZE];
static int comIdxIn, comIdxCur, comIdxOut;

static SDL_AudioStream *stream;

// A sample's byte, inside the ROM.
static int romByte(uint32_t offset)
{
    return offset < samplesSize ? samplesBuffer[offset] : 0;
}

static int step16(Sample *ps)
{
    uint32_t ph0 = ps->phaseLO, idx = (ph0 >> 12) << 1;
    int d0, d1, data;

    d0 = (int16_t)(romByte(ps->pSample + idx) | romByte(ps->pSample + idx + 1) << 8);
    if (idx + 2 < ps->ssize)
        d1 = (int16_t)(romByte(ps->pSample + idx + 2) | romByte(ps->pSample + idx + 3) << 8);
    else
        d1 = d0;
    data = d0 + (((d1 - d0) * (int)(ph0 & 0xfff)) >> 12);
    data = (data * ps->vmaster) >> 12;
    ph0 += ps->phinc;
    if (ph0 >= ps->phEndLO)
    {
        if (!(ps->flags & LOOP))
            ps->flags &= ~ENABLE;
        else
            ph0 = 0;
    }
    ps->phaseLO = ph0;
    return data;
}

static int step8(Sample *ps)
{
    uint32_t ph0 = ps->phaseLO, idx = ph0 >> 12;
    int d0, d1, data;

    d0 = (int8_t)romByte(ps->pSample + idx++);
    d1 = idx < ps->ssize ? (int8_t)romByte(ps->pSample + idx) : d0;
    data = d0 + (((d1 - d0) * (int)(ph0 & 0xfff)) >> 12);
    // 8-bit data, scaled to 16 bits with the volume.
    data = (data * ps->vmaster) >> (12 - 8);
    ph0 += ps->phinc;
    // A looping 8-bit sample wraps as the others do (the module read on
    // past its end, through the next samples).
    if (ph0 >= ps->phEndLO)
    {
        if (!(ps->flags & LOOP))
            ps->flags &= ~ENABLE;
        else
            ph0 = 0;
    }
    ps->phaseLO = ph0;
    return data;
}

static int16_t adpcmDecode(Sample *ps, int delta)
{
    int index = ps->aindex, step, sign, vout, vpdiff;

    delta &= 0x0f;
    step = stepsizeTable[index];
    sign = delta & 8;
    delta &= 7;
    index += indexTable[delta];
    if (index < 0)
        index = 0;
    if (index > 88)
        index = 88;
    vout = ps->data0;
    vpdiff = step >> 3;
    if (delta & 4)
        vpdiff += step;
    if (delta & 2)
        vpdiff += step >> 1;
    if (delta & 1)
        vpdiff += step >> 2;
    vout += sign ? -vpdiff : vpdiff;
    if (vout > 32767)
        vout = 32767;
    else if (vout < -32768)
        vout = -32768;
    ps->aindex = (uint8_t)index;
    return (int16_t)vout;
}

static int stepAdpcm(Sample *ps)
{
    uint32_t ph0 = ps->phaseLO, ph1;
    int data;

    if (!(ps->flags & ADV))
    {
        uint32_t idx = (ph0 >> 12) + (ps->phaseHI << 20);
        data = romByte(ps->pSample + (idx >> 1));
        if (!(idx & 1))
            data >>= 4;
        ps->data1 = adpcmDecode(ps, data);
    }
    data = ps->data0 + (((ps->data1 - ps->data0) * (int)(ph0 & 0xfff)) >> 12);
    data = (data * ps->vmaster) >> 12;
    ph1 = ph0 + ps->phinc;
    if (0xffffffffu - ph0 < ps->phinc)
        ps->phaseHI++;
    ps->phaseLO = ph1;
    if (!((ph0 ^ ph1) & 0x1000))
    {
        ps->flags |= ADV;
        return data;
    }
    ps->flags &= ~ADV;
    ps->data0 = ps->data1;
    if (ph1 >= ps->phEndLO && ps->phaseHI >= ps->phEndHI)
    {
        if (ps->flags & LOOP)
        {
            ps->phaseLO = ps->phaseHI = 0;
            ps->data0 = 0;
            ps->aindex = 0;
        }
        else
            ps->flags &= ~ENABLE;
    }
    return data;
}

static void samplerReset(void)
{
    vglobal = 0xb33; // 0x1000 * 0.7
    vbalance[0] = vbalance[1] = vbalance[2] = vbalance[3] = 0xb33;
    soundChannels = 0;
    memset(samples, 0, sizeof(samples));
    for (int i = 0; i < NUMCAN; i++)
        samples[i].hfile = samplesBuffer;
    comIdxIn = comIdxCur = comIdxOut = 0;
    nsframes = 0;
}

static void setupChannel(int channel, int sample, int loop)
{
    Sample *ps;
    const SHeader *psh;

    if (!dirSound || channel < 0 || channel >= NUMCAN || sample >= soundDirSize)
        return;
    ps = &samples[channel];
    psh = &dirSound[sample];
    ps->flags = 0;
    soundChannels &= ~(1u << channel);
    ps->phaseLO = ps->phaseHI = 0;
    ps->vmaster = 0;
    ps->vfin = (0x0fff * psh->vnorm) >> 14;
    ps->aindex = 0;
    ps->data0 = ps->data1 = 0;
    ps->pSample = psh->start;
    ps->nsample = (int16_t)sample;
    if (loop)
        ps->flags |= LOOP;
    ps->ssize = 2 * psh->ns;
    ps->phEndLO = psh->ns << 12;
    ps->phEndHI = psh->ns >> 20;
    ps->phinc = psh->phinc;
    ps->ramping = psh->rkon >> 2;
    switch (psh->flags & ST_MASK)
    {
    case ST_16BIT:
        ps->step = step16;
        break;
    case ST_ADPCM:
        ps->flags |= ST_ADPCM;
        ps->step = stepAdpcm;
        ps->ssize = (ps->ssize + 1) >> 1;
        break;
    case ST_8BIT:
        ps->flags |= 2; // STI_8BIT
        ps->step = step8;
        break;
    default:
        return;
    }
    ps->hfile = samplesBuffer + ps->pSample;
    ps->vpan[0] = ps->vpan[1] = ps->vpan[2] = ps->vpan[3] = 0xfff;
    ps->flags |= ENABLE;
}

static void startChannel(int channel)
{
    if (channel >= 0 && channel < NUMCAN)
        soundChannels |= 1u << channel;
}

static void setFreq(int channel, int dph)
{
    if (channel < 0 || channel >= NUMCAN || !dirSound)
        return;
    samples[channel].phinc = (uint16_t)(((dph * dirSound[samples[channel].nsample].phinc) >> 12) & 0x7fff);
}

static void setPanning(int channel, const uint16_t *p)
{
    if (channel < 0 || channel >= NUMCAN)
        return;
    memcpy(samples[channel].vpan, p, sizeof(samples[channel].vpan));
}

static void setVolRate(int channel, int vol, int rate)
{
    Sample *ps;
    int vf;

    if (channel < 0 || channel >= NUMCAN || !dirSound)
        return;
    ps = &samples[channel];
    if (rate == 0)
        rate = 1;
    vf = (vol * dirSound[ps->nsample].vnorm) >> 14;
    if (ps->vmaster > vf)
        rate = -rate;
    ps->ramping = (int16_t)rate;
    ps->vfin = (int16_t)vf;
}

static void keyOff(int frate, uint32_t mask)
{
    int16_t rate = (int16_t)frate;

    mask &= soundChannels;
    for (int i = 0; i < NUMCAN; i++)
        if (mask & (1u << i))
        {
            samples[i].vfin = 0;
            samples[i].ramping = -rate;
            samples[i].flags |= KEYOFF;
        }
}

// The commands validated since the last buffer.
static void commproc(void)
{
    while (comIdxIn != comIdxOut)
    {
        SoundComm *pc = &combuf[comIdxOut];
        comIdxOut = (comIdxOut + 1) & COMBUF_MASK;
        switch (pc->command)
        {
        case SC_SETUP:
            setupChannel(pc->channel, pc->parms[0], pc->parms[1]);
            break;
        case SC_START:
            startChannel(pc->channel);
            break;
        case SC_FREQ:
            setFreq(pc->channel, pc->parms[0]);
            break;
        case SC_PAN:
            setPanning(pc->channel, pc->parms);
            break;
        case SC_VOL:
            setVolRate(pc->channel, pc->parms[0], pc->parms[1]);
            break;
        case SC_KOFF:
            keyOff(pc->parms[0], pc->parms[1]);
            break;
        case SC_STOP:
            soundChannels &= ~(uint32_t)pc->parms[0];
            break;
        }
    }
}

static int rampChannel(Sample *ps)
{
    int v = ps->vmaster + ps->ramping, vf = ps->vfin;

    if ((ps->ramping > 0 && v > vf) || (ps->ramping < 0 && v < vf))
    {
        v = vf;
        ps->ramping = 0;
    }
    ps->vmaster = (int16_t)v;
    if (!v && (ps->flags & KEYOFF))
    {
        ps->flags = 0;
        return 0;
    }
    return 1;
}

static void ramping(void)
{
    for (int i = 0; i < NUMCAN; i++)
        if ((soundChannels & (1u << i)) && samples[i].ramping && !rampChannel(&samples[i]))
            soundChannels &= ~(1u << i);
}

// One of the module's buffers, folded into stereo: front left and rear
// left, front right and rear right.
static void sampler(int16_t *dst)
{
    for (int n = 0; n < FRAMES_PER_BUFFER; n++)
    {
        int v[4] = {0, 0, 0, 0}, out[4];

        if (!(nsframes & 0x1f))
            ramping();
        nsframes += 4;
        for (int i = 0; i < NUMCAN; i++)
        {
            Sample *ps = &samples[i];
            int value;
            if (!(soundChannels & (1u << i)) || !ps->step)
                continue;
            value = ps->step(ps);
            if (!(ps->flags & ENABLE))
                soundChannels &= ~(1u << i);
            for (int j = 0; j < 4; j++)
                v[j] += value * ps->vpan[j];
        }
        for (int i = 0; i < 4; i++)
        {
            int value = (v[i] >> 12) * vbalance[i];
            value = (value >> 12) * vglobal;
            out[i] = value >> 12;
        }
        for (int i = 0; i < 2; i++)
        {
            int value = (out[i] + out[i + 2]) * 3 / 4;
            dst[n * 2 + i] = (int16_t)(value < -0x7fff ? -0x7fff : value > 0x7fff ? 0x7fff : value);
        }
    }
}

static void SDLCALL feed(void *userdata, SDL_AudioStream *s, int additional, int total)
{
    static FILE *dump;
    static int dumpChecked;
    int16_t buffer[FRAMES_PER_BUFFER * 2];

    (void)userdata;
    (void)total;
    // GAELCO_SOUND_DUMP=file: the output, raw (48 kHz, 16-bit stereo).
    if (!dumpChecked)
    {
        const char *path = getenv("GAELCO_SOUND_DUMP");
        dumpChecked = 1;
        if (path)
            dump = fopen(path, "wb");
    }
    while (additional > 0)
    {
        pthread_mutex_lock(&lock);
        commproc();
        sampler(buffer);
        pthread_mutex_unlock(&lock);
        SDL_PutAudioStreamData(s, buffer, sizeof(buffer));
        if (dump)
            fwrite(buffer, sizeof(buffer), 1, dump);
        additional -= sizeof(buffer);
    }
}

int gaelcoSoundStart(void)
{
    static const SDL_AudioSpec spec = {SDL_AUDIO_S16LE, 2, RATE};

    if (stream)
        return 0;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        log_error("Gaelco: no audio: %s", SDL_GetError());
        return 0;
    }
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!stream)
    {
        log_error("Gaelco: no audio stream: %s", SDL_GetError());
        return 0;
    }
    SDL_ResumeAudioStreamDevice(stream);
    return 0;
}

int gaelcoSoundStop(void)
{
    if (stream)
        SDL_PauseAudioStreamDevice(stream);
    return 0;
}

int gaelcoSoundLoad(const void *data, int size)
{
    uint8_t *copy;

    if (size <= 0 || !(copy = malloc(size)))
        return 0;
    memcpy(copy, data, size);
    pthread_mutex_lock(&lock);
    free(samplesBuffer);
    samplesBuffer = copy;
    samplesSize = (uint32_t)size;
    // The directory: the first sample begins right after its last entry.
    dirSound = (const SHeader *)samplesBuffer;
    soundDirSize = size >= (int)sizeof(SHeader) ? (int)(dirSound->start / sizeof(SHeader)) : 0;
    samplerReset();
    pthread_mutex_unlock(&lock);
    printf("Gaelco: sample ROM of %d bytes, %d samples\n", size, soundDirSize);
    return 0;
}

int gaelcoSoundAddCommand(int command, int channel, int p0p1, int p2p3)
{
    static int trace = -1;

    if (trace < 0)
        trace = getenv("GAELCO_SOUND_TRACE") != NULL;
    if (trace)
        printf("Gaelco sound: command %d channel %d %08x %08x\n", command, channel, p0p1, p2p3);
    pthread_mutex_lock(&lock);
    int x = (comIdxCur + 1) & COMBUF_MASK;
    if (x != comIdxOut)
    {
        SoundComm *ps = &combuf[comIdxCur];
        ps->command = (int16_t)command;
        ps->channel = (int16_t)channel;
        ps->parms[0] = (uint16_t)p0p1;
        ps->parms[1] = (uint16_t)(p0p1 >> 16);
        ps->parms[2] = (uint16_t)p2p3;
        ps->parms[3] = (uint16_t)(p2p3 >> 16);
        comIdxCur = x;
    }
    pthread_mutex_unlock(&lock);
    return 1;
}

static int volume(float x)
{
    return (int16_t)(x * (float)0x0fff);
}

int gaelcoSoundCommand(int command, int *parms, int *result)
{
    const float *f = (const float *)parms;


    switch (command)
    {
    case IOINTF_CMD_MASTERVOL:
        vglobal = volume(f[0]);
        break;
    case IOINTF_CMD_MASTERBALANCE:
        for (int i = 0; i < 4; i++)
            vbalance[i] = volume(f[i]);
        break;
    case IOINTF_CMD_GETCHANNELSTATUS:
        *result = (int)soundChannels;
        return 1;
    case IOINTF_CMD_SOUNDSTART:
        break;
    case IOINTF_CMD_FLUSHCOMMBUF:
        pthread_mutex_lock(&lock);
        comIdxIn = comIdxCur;
        pthread_mutex_unlock(&lock);
        break;
    case IOINTF_CMD_BUFFERREADY:
        // (the module never answered it: always ready)
        *result = 1;
        return 1;
    case IOINTF_CMD_BUFFERREADY_2003:
        // Tokyo Cop's module: the commands validated have been run. The
        // game sends no sound until they have; the queue drops what it has
        // no room for.
        if (gaelcoCurrentGame()->io != GAELCO_IO_PCI_CONFIG)
            return 0;
        *result = 1;
        return 1;
    default:
        return 0;
    }
    *result = 1;
    return 1;
}
