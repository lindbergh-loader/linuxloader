// Host-side regression checks, with no redistributed game/firmware bytes.
/* Build and run:
   cc -O2 -ffunction-sections -fdata-sections -Isrc/SDL3 -Isrc/minhook/include
      tools/tests/igs-sd2.c src/loader/igs/igsArm7.c -Wl,--gc-sections -o /tmp/igs-sd2-check
   /tmp/igs-sd2-check '/path/to/roms/SD2_EPROM.dec.bin'
*/
#include <assert.h>
#include <stdarg.h>
#include "../../src/loader/igs/igsSd2.c"

static float testForce;
static unsigned testMotorCalls;
int evdevFfbOpen(const char *who) { (void)who; return 1; }
void evdevFfbConstant(float force) { testForce = force; testMotorCalls++; }

int logGeneric(int level, const char *file, int line, const char *message, ...)
{
    (void)level; (void)file; (void)line;
    va_list ap;
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
    fputc('\n', stderr);
    return 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    boardStart(argv[1]);
    Sd2Board *b = board;
    Arm7 *cpu = &b->cpu;
    assert(!cpu->halted);
    assert(b->commandOut[6] == 0x80);
    assert(b->handlers[3] == 0x10000110);
    assert(b->handlers[6] == 0x1000000c);
    assert(b->handlers[7] == 0x10000064);
    assert((b->enabled & 0xc8) == 0xc8);

    // A real queued UART packet must decode every magnitude in both
    // directions, ignore a broken checksum, and stop on disable/shutdown.
    putWord(0x20006b60, 1);
    putWord(0x20006b64, 9);
    for (int i = 0; i < 8; i++) putWord(0x20006b68 + i * 4, 0);
    for (unsigned raw = 0; raw < 256; raw++)
    {
        putWord(0x20006b68, raw);
        putWord(0x20006b84, raw);
        unsigned before = testMotorCalls;
        memoryWrite(cpu, 0xfffa4000, 0x40, 4);
        unsigned magnitude = ((raw & 1) << 5) | ((raw & 2) << 3) |
            ((raw & 4) << 1) | ((raw & 8) >> 1) | ((raw & 16) >> 3) | ((raw & 32) >> 5);
        float expected = (raw & 128) ? ((raw & 64) ? 1.f : -1.f) * magnitude / 63.f : 0;
        assert(testMotorCalls == before + 1 && testForce == expected);
    }
    unsigned before = testMotorCalls;
    putWord(0x20006b84, 0); // invalid checksum
    memoryWrite(cpu, 0xfffa4000, 0x40, 4);
    assert(testMotorCalls == before);
    stopSd2Motor();
    assert(testForce == 0);

    // GPIO latches are directional: PC writes do not overwrite board output.
    b->command[6] = 0xa5;
    assert(b->commandOut[6] == 0x80);
    // Command-read service writes two words and interworks back to Thumb.
    Arm7 saved = *cpu;
    cpu->r[0] = 0x2001f000;
    cpu->r[1] = 0x12345678;
    cpu->r[14] = 0x1000abcd;
    maskService(cpu, 0x20c);
    assert(arm7Read32(cpu, 0x2001f000) == 0xa5);
    assert(arm7Read32(cpu, 0x2001f004) == 0);
    assert(cpu->r[0] == 0xa5 && cpu->r[1] == 0x12345678);
    assert(cpu->r[15] == 0x1000abcc && (cpu->cpsr & 0x20));
    *cpu = saved;

    // Real firmware handshakes, including sequence wrap. Force timer IRQs
    // deterministically instead of depending on the host's scheduling.
    b->shared[2] = 1;
    for (int frame = 0; frame < 140; frame++)
    {
        uint8_t sequence = b->commandOut[6];
        uint8_t input = (uint8_t)((sequence + 1) | 0x80);
        uint8_t expected = (uint8_t)((sequence + 2) | 0x80);
        uint32_t key;
        key = arm7Read32(cpu, 0x200000c4);
        memcpy(b->shared + 0x18, &key, 4); // empty encoded PC command buffer
        memcpy(b->shared + 0x80, &key, 4);
        b->command[6] = input;
        b->inflight = 1;
        b->pending |= 1u << 3;
        for (int i = 0; i < 100 && b->commandOut[6] != expected && !cpu->halted; i++)
        {
            b->pending |= 0xc0;
            boardStep(100000);
        }
        assert(!cpu->halted);
        if (b->commandOut[6] != expected)
            fprintf(stderr, "frame %d input %02x expected %02x got %02x pc %08x state %u inflight %d\n",
                    frame, input, expected, b->commandOut[6], cpu->r[15], arm7Read32(cpu, 0x20000138), b->inflight);
        assert(b->commandOut[6] == expected);
        assert(b->shared[0x3000] == expected);
        assert(!b->inflight);
    }
    assert(arm7Read32(cpu, 0x20000150) > 0);
    assert(arm7Read32(cpu, 0x20000154) > 0);
    // Unknown internal-ROM code must be rejected, not treated as a success.
    maskService(cpu, 0x3ffc);
    assert(cpu->halted);
    puts("SD2: boot, services, motor packets/shutdown, IRQs and 140 firmware exchanges passed");
    return 0;
}
