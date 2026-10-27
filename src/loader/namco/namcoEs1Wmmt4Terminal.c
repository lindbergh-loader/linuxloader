// Wangan Midnight Maximum Tune 4's terminal, from the Pacloader fork
// (es1Wmmt4Terminal.cpp). A drive cabinet's boot check waits for the shop's
// terminal (the card and ghost machine) on the cabinet LAN: it announces
// itself on the multicast group 225.0.0.1:50765 every frame, as a protobuf
// message (its car, course, heartbeat with the drive's serial, batch
// settings, information) framed with its PCB id, length and a CRC32. The
// cabinet empties its peer entry for the terminal each frame and fills it from
// that frame's datagram: it is sent every 4 ms.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "namcoEs1.h"
#include "../log/log.h"

#define GROUP "225.0.0.1"
#define PORT 50765
#define INTERVAL_NS 4000000L

typedef struct
{
    uint8_t data[512];
    size_t len;
} Packet;

static char heartbeatSerial[16];

static void varint(Packet *p, uint64_t value)
{
    do
    {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        if (value)
            byte |= 0x80;
        if (p->len < sizeof(p->data))
            p->data[p->len++] = byte;
    } while (value);
}

static void fieldInt(Packet *p, unsigned field, int32_t value)
{
    varint(p, (uint64_t)field << 3);
    varint(p, (uint64_t)(int64_t)value);
}

static void fieldBytes(Packet *p, unsigned field, const void *data, size_t n)
{
    varint(p, (uint64_t)field << 3 | 2);
    varint(p, n);
    if (p->len + n <= sizeof(p->data))
    {
        memcpy(p->data + p->len, data, n);
        p->len += n;
    }
}

static void fieldString(Packet *p, unsigned field, const char *s)
{
    fieldBytes(p, field, s, strlen(s));
}

static void fieldMessage(Packet *p, unsigned field, const Packet *m)
{
    fieldBytes(p, field, m->data, m->len);
}

static void message(Packet *out, uint32_t frame, uint32_t now)
{
    Packet car = {0}, course = {0}, heartbeat = {0}, settings = {0}, information = {0};

    fieldInt(&car, 1, 0); // car_id
    fieldInt(&car, 2, 0); // region_id
    fieldString(&car, 3, "");
    fieldString(&car, 4, "");
    fieldInt(&car, 5, 0); // manufacturer
    fieldInt(&car, 7, 0); // visual_model
    fieldInt(&car, 8, 0); // default_color
    for (unsigned field = 9; field <= 26; field++)
        fieldInt(&car, field, 0);
    fieldInt(&car, 27, 47); // level
    fieldMessage(out, 2, &car);

    fieldInt(&course, 6, -1); // player_no
    fieldInt(&course, 7, 0);  // player_max
    fieldMessage(out, 3, &course);

    fieldInt(&heartbeat, 1, (int32_t)frame);
    fieldString(&heartbeat, 2, heartbeatSerial);
    fieldInt(&heartbeat, 3, 1);
    fieldMessage(out, 5, &heartbeat);

    fieldInt(out, 6, 0); // batch_setting_state
    // coin chute, card/game/continue/full course costs, free play, wins and
    // remains, event mode and distance, close type, 8 x 96, software revision
    static const int32_t values[] = {1, 3, 1, 1, 1, 1, 1, 0, 0, 2, 96, 96, 96, 96, 96, 96, 96, 96, 11018};
    for (unsigned field = 1; field <= sizeof(values) / sizeof(values[0]); field++)
        fieldInt(&settings, field, values[field - 1]);
    fieldInt(&settings, 20, (int32_t)now);
    fieldMessage(out, 8, &settings);

    fieldInt(&information, 1, 14);
    fieldInt(&information, 2, -1);
    for (unsigned field = 3; field <= 6; field++)
        fieldInt(&information, field, 0);
    fieldString(&information, 7, "");
    for (unsigned field = 8; field <= 14; field++)
        fieldInt(&information, field, 0);
    fieldMessage(out, 9, &information);
}

static uint32_t crc32Mpeg(const uint8_t *data, size_t n)
{
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= (uint32_t)data[i] << 24;
        for (int bit = 0; bit < 8; bit++)
            crc = crc & 0x80000000 ? crc << 1 ^ 0x04c11db7 : crc << 1;
    }
    return crc;
}

// 01, the terminal's PCB id (4), the length (little-endian, header
// included), the message, its CRC32 (big-endian).
static void packet(Packet *p, uint32_t frame)
{
    Packet body = {0};

    message(&body, frame, (uint32_t)time(NULL));
    p->len = 0;
    p->data[p->len++] = 1;
    p->data[p->len++] = 4;
    p->len += 2;
    memcpy(p->data + p->len, body.data, body.len);
    p->len += body.len;
    p->data[2] = p->len & 0xff;
    p->data[3] = p->len >> 8;
    uint32_t crc = crc32Mpeg(p->data, p->len);
    for (int i = 3; i >= 0; i--)
        p->data[p->len++] = crc >> (8 * i);
}

static void *terminalThread(void *arg)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int one = 1, ttl = 255;
    struct sockaddr_in local = {0}, group = {0};
    struct timespec next;
    uint32_t frame = 0, failures = 0;
    Packet p;

    (void)arg;
    if (s < 0)
    {
        log_error("WMMT4 terminal: no socket");
        return NULL;
    }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    local.sin_family = AF_INET;
    local.sin_port = htons(PORT);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&local, sizeof(local)) != 0)
        log_warn("WMMT4 terminal: cannot bind UDP %d", PORT);
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    // The cabinet LAN stays on this machine: on the loopback interface,
    // where the cabinet's sockets are made to join the group too
    // (namcoEs1Wmmt4.c). A firewall drops looped multicast on the others.
    struct in_addr loopback = {htonl(INADDR_LOOPBACK)};
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &loopback, sizeof(loopback));
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &one, sizeof(one));
    // It only sends: no copy of the group's traffic queued for it.
    int zero = 0;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_ALL, &zero, sizeof(zero));
    group.sin_family = AF_INET;
    group.sin_port = htons(PORT);
    inet_pton(AF_INET, GROUP, &group.sin_addr);

    clock_gettime(CLOCK_MONOTONIC, &next);
    for (;;)
    {
        packet(&p, ++frame);
        if (sendto(s, p.data, p.len, 0, (struct sockaddr *)&group, sizeof(group)) < 0)
        {
            if (++failures <= 3)
                log_warn("WMMT4 terminal: send failed");
        }
        else
            failures = 0;
        next.tv_nsec += INTERVAL_NS;
        if (next.tv_nsec >= 1000000000L)
        {
            next.tv_sec++;
            next.tv_nsec -= 1000000000L;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    return NULL;
}

void namcoEs1Wmmt4StartTerminal(const char *serial)
{
    static int started;
    pthread_t thread;

    if (started)
        return;
    started = 1;
    snprintf(heartbeatSerial, sizeof(heartbeatSerial), "%s", serial);
    if (pthread_create(&thread, NULL, terminalThread, NULL) == 0)
        pthread_detach(thread);
    else
        log_error("WMMT4 terminal: cannot start its thread");
}
