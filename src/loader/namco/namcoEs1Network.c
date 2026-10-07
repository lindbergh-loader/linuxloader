// The cabinet's network administration, which the game runs as root through
// su -c / sudo (networkCommands): answered here instead, as the Pacloader
// fork does (es1Network.cpp). The other root commands are not run.
//
// - "perl ./data/ifconfig.pl > file": the script reports eth0 as one line,
//   "index a b c d m m m m mac0..mac5 link"; the game reads it back for its
//   address (without it: "Recovering network connections"). Written from the
//   host's first interface with an IPv4 address, as the game last set it.
// - "ifconfig eth0 a.b.c.d netmask m.m.m.m ...": the address the game gives
//   itself (192.168.<group>.<pcb>), kept for the next report.
// - arping.sh / pinger.pl: the LAN checks, which find nobody on an isolated
//   cabinet: their reports are written empty (an IP conflict check reads
//   arping.txt; the LAN start waits for ping-reports.txt to exist).

#include <arpa/inet.h>
#include <ctype.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "namcoEs1.h"
#include "../log/log.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized;
static unsigned char address[4] = {127, 0, 0, 1}, mask[4] = {255, 0, 0, 0}, mac[6];

// The host's first interface with an IPv4 address (not loopback). Caller
// holds lock.
static void initialize(void)
{
    struct ifaddrs *list;

    if (initialized)
        return;
    initialized = 1;
    if (getifaddrs(&list) != 0)
        return;
    for (struct ifaddrs *i = list; i; i = i->ifa_next)
    {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !i->ifa_netmask)
            continue;
        memcpy(address, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, 4);
        memcpy(mask, &((struct sockaddr_in *)i->ifa_netmask)->sin_addr, 4);
        int s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s >= 0)
        {
            struct ifreq ifr = {0};
            snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", i->ifa_name);
            if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0)
                memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
            close(s);
        }
        log_info("Namco: eth0 is %s, %u.%u.%u.%u", i->ifa_name, address[0], address[1], address[2], address[3]);
        break;
    }
    freeifaddrs(list);
}

// The file a command's output goes to ("... > file"), or 0.
static int redirectPath(const char *command, char *path, size_t size)
{
    const char *r = strchr(command, '>');
    if (!r)
        return 0;
    for (r++; isspace((unsigned char)*r); r++)
        ;
    size_t n = strlen(r);
    while (n && (isspace((unsigned char)r[n - 1]) || r[n - 1] == '\'' || r[n - 1] == '"'))
        n--;
    if (*r == '\'' || *r == '"')
    {
        r++;
        n--;
    }
    // The game's directory is the cabinet's root for these.
    while (n && *r == '/')
    {
        r++;
        n--;
    }
    if (n == 0 || n >= size)
        return 0;
    memcpy(path, r, n);
    path[n] = '\0';
    return 1;
}

static void writeFile(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f)
    {
        log_warn("Namco: cannot write %s", path);
        return;
    }
    fputs(text, f);
    fclose(f);
}

static void interfaceReport(const char *command)
{
    char path[512], line[160];

    if (!redirectPath(command, path, sizeof(path)))
        return;
    pthread_mutex_lock(&lock);
    initialize();
    snprintf(line, sizeof(line), "0 %u %u %u %u %u %u %u %u %u %u %u %u %u %u 1\n", address[0], address[1],
             address[2], address[3], mask[0], mask[1], mask[2], mask[3], mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    pthread_mutex_unlock(&lock);
    writeFile(path, line);
}

// "ifconfig eth0 <address> [netmask <mask>] ...".
static void setInterface(const char *command)
{
    const char *p = strstr(command, "eth0");
    unsigned a[4], m[4];

    pthread_mutex_lock(&lock);
    initialize();
    if (p && sscanf(p, "eth0 %u.%u.%u.%u", &a[0], &a[1], &a[2], &a[3]) == 4)
        for (int i = 0; i < 4; i++)
            address[i] = a[i];
    if (p && (p = strstr(p, "netmask")) && sscanf(p, "netmask %u.%u.%u.%u", &m[0], &m[1], &m[2], &m[3]) == 4)
        for (int i = 0; i < 4; i++)
            mask[i] = m[i];
    pthread_mutex_unlock(&lock);
}

// Nobody else on the LAN: no conflict, nobody pinged.
static void lanReports(void)
{
    // A peer that answers nothing, rather than a stale address of our own.
    writeFile("save0/tmp/arp_ip.txt", "192.168.3.254\n");
    writeFile("save1/tmp/arp_ip.txt", "192.168.3.254\n");
    writeFile("save0/tmp/arping.txt", "");
    writeFile("save1/tmp/arping.txt", "");
    writeFile("save0/ping-reports.txt", "");
    writeFile("save1/ping-reports.txt", "");
}

void namcoEs1HostInterface(unsigned char addressOut[4], unsigned char maskOut[4], unsigned char macOut[6])
{
    pthread_mutex_lock(&lock);
    initialize();
    memcpy(addressOut, address, 4);
    memcpy(maskOut, mask, 4);
    memcpy(macOut, mac, 6);
    pthread_mutex_unlock(&lock);
}

void namcoEs1NetworkCommand(const char *command)
{
    if (strstr(command, "ifconfig.pl") && strchr(command, '>'))
        interfaceReport(command);
    else if (strstr(command, "ifconfig eth0"))
        setInterface(command);
    else if (strstr(command, "arping") || strstr(command, "pinger.pl"))
        lanReports();
}
