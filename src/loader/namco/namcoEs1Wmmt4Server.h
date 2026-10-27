#ifndef NAMCO_ES1_WMMT4_SERVER_H
#define NAMCO_ES1_WMMT4_SERVER_H

#include <stddef.h>
#include <stdint.h>

// Wangan Midnight Maximum Tune 4's online services, answered by the loader:
// ALL.Net, Mucha and the game server (its protobuf RPCs), for the cards'
// users, cars and progress (namcoEs1Wmmt4Server.c), reached through
// namcoEs1Wmmt4Net.c.

// The services' address: the shop's router on the cabinet's LAN, whose
// connections are the loader's server.
#define WMMT4_SERVER_ADDRESS "192.168.92.254"

typedef struct
{
    int status;              // HTTP status
    const char *contentType; // static
    uint8_t *body;           // malloc'd (or NULL)
    size_t length;
} Wmmt4Response;

// One request: the host it was sent to, its path ("/method/load_user"...)
// and body.
void wmmt4ServerHandle(const char *host, const char *path, const uint8_t *body, size_t length,
                       Wmmt4Response *response);

// The game server's RPCs and resources (namcoEs1Wmmt4Rpc.c).
void wmmt4RpcHandle(const char *path, const uint8_t *body, size_t length, Wmmt4Response *response);

// The Banapassport card reader (namcoEs1Wmmt4Card.c).
void namcoEs1Wmmt4CardInstall(void);

// The redirections to the loader's server and the server itself.
void namcoEs1Wmmt4NetInstall(void);

// WMMT4_SERVER_TRACE set: the requests and answers on stderr.
extern int wmmt4ServerTrace;

#endif // NAMCO_ES1_WMMT4_SERVER_H
