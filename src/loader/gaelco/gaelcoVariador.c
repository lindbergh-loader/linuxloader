// Ring Riders' motion drive (see gaelco.h): the frequency drive (variador)
// that tilts the motorbike, on the I/O board's RS485 port.
//
// The game sets the drive up at boot (writes its parameters, reads each
// back, and programs again those that differ, a few times over: about a
// minute of time-outs without the drive) and drives the platform with it
// during the game. Its frames are ASCII (EI-Bisynch):
//   read:  EOT a a a a  p p q q  ENQ           (parameter p.q)
//          answer: STX p p q q value ETX BCC
//   write: EOT a a a a  STX p p q q value ETX BCC
//          answer: ACK (NAK: refused)
// with BCC the XOR of the bytes after STX up to ETX (plus 0x20 when below
// it). The drive here keeps what it is written and answers with it: the
// set-up passes at once, the platform's commands are taken (there is no
// motion).

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gaelco.h"
#include "../patching/flowControl.h"

#define EOT 0x04
#define ENQ 0x05
#define STX 0x02
#define ETX 0x03
#define ACK 0x06

#define MAX_PARAMETERS 64

static struct
{
    char key[5];
    char value[16];
} parameters[MAX_PARAMETERS];
static int parameterCount;

static char frame[64];
static int frameLength;
static int bccPending; // the write frame's ETX seen, its BCC next

static unsigned char answer[64];
static int answerLength, answerPos;

static int trace = -1;

static char *parameter(const char *key, int create)
{
    for (int i = 0; i < parameterCount; i++)
        if (!strcmp(parameters[i].key, key))
            return parameters[i].value;
    if (!create || parameterCount == MAX_PARAMETERS)
        return NULL;
    memcpy(parameters[parameterCount].key, key, 4);
    parameters[parameterCount].value[0] = '\0';
    return parameters[parameterCount++].value;
}

static void reply(const unsigned char *bytes, int length)
{
    memcpy(answer, bytes, length);
    answerLength = length;
    answerPos = 0;
}

// A whole frame from the game: its answer.
static void frameDone(void)
{
    char key[5] = {0};
    char *value = NULL;

    if (frameLength < 10 || frame[0] != EOT)
        return;
    if (frame[5] == STX)
    {
        // A write: the key, then the value up to ETX.
        char *end = memchr(frame + 6, ETX, frameLength - 6);
        unsigned char ack = ACK;
        memcpy(key, frame + 6, 4);
        if (end && (value = parameter(key, 1)))
        {
            int n = (int)(end - (frame + 10));
            if (n > (int)sizeof(parameters[0].value) - 1)
                n = sizeof(parameters[0].value) - 1;
            memcpy(value, frame + 10, n);
            value[n] = '\0';
        }
        if (trace)
            printf("Gaelco drive: %s := %s\n", key, value ? value : "?");
        reply(&ack, 1);
    }
    else if (frame[9] == ENQ)
    {
        unsigned char out[32];
        int n = 0, bcc = 0;
        memcpy(key, frame + 5, 4);
        value = parameter(key, 0);
        out[n++] = STX;
        memcpy(out + n, key, 4);
        n += 4;
        if (value)
        {
            memcpy(out + n, value, strlen(value));
            n += strlen(value);
        }
        out[n++] = ETX;
        for (int i = 1; i < n; i++)
            bcc ^= out[i];
        out[n++] = (unsigned char)(bcc < 0x20 ? bcc + 0x20 : bcc);
        if (trace)
            printf("Gaelco drive: %s ? %s\n", key, value ? value : "(none)");
        reply(out, n);
    }
}

void gaelcoVariadorSend(int byte)
{
    if (trace < 0)
        trace = getenv("GAELCO_DRIVE_TRACE") != NULL;
    byte &= 0xff;
    if (byte == 0)
        return; // the string's end, which the game sends too
    if (byte == EOT)
    {
        frameLength = 0;
        bccPending = 0;
        answerLength = answerPos = 0;
    }
    if (frameLength < (int)sizeof(frame))
        frame[frameLength++] = (char)byte;
    if (bccPending)
    {
        bccPending = 0;
        frameDone();
    }
    else if (byte == ETX && frameLength > 5 && frame[5] == STX)
        bccPending = 1;
    else if (byte == ENQ)
        frameDone();
}

int gaelcoVariadorAvailable(void)
{
    return answerPos < answerLength;
}

int gaelcoVariadorPeek(void)
{
    return answerPos < answerLength ? answer[answerPos] : 0;
}

void gaelcoVariadorNext(void)
{
    if (answerPos < answerLength)
        answerPos++;
}

// The game's RS485 string read: the answer's bytes, at once (the game's
// waits 200 ms for each, a minute at boot with the window frozen).
__attribute__((force_align_arg_pointer)) static int rs485GetString(void *self, char *buf, int max)
{
    int n = 0;

    (void)self;
    while (n < max && gaelcoVariadorAvailable())
    {
        buf[n++] = (char)gaelcoVariadorPeek();
        gaelcoVariadorNext();
    }
    return n;
}

// Ring Riders' iniPlataforma (0x080bf60c): its state, without the drive's
// baud rates and parameters (the drive here keeps what it is sent later).
__attribute__((force_align_arg_pointer)) static void platformInit(void)
{
    *(uint32_t *)0x08169a64 = 0;
    *(uint32_t *)0x08169abc = 0; // plataformaBloqueada
    *(uint32_t *)0x08169ac0 = 0; // estadoPlataforma
    *(uint32_t *)0x08169a5c = 0;
    *(uint32_t *)0x08169a68 = 0x17;
    *(uint32_t *)0x08130200 = 0x3f666666; // 0.9f
}

void gaelcoVariadorInstall(void)
{
    const GaelcoGame *g = gaelcoCurrentGame();

    if (g->rs485GetString)
        detourFunction(g->rs485GetString, rs485GetString);
    if (g->platformInit)
        detourFunction(g->platformInit, platformInit);
}
