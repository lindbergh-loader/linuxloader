// The way to Wangan Midnight Maximum Tune 4's online services, all answered
// by the loader (namcoEs1Wmmt4Server.c):
// - its names (ALL.Net's naominet.jp, the shop's routers *.loc, Mucha's
//   front) resolve to the shop's router on the cabinet's LAN, 192.168.92.254
//   (ALL.Net's library refuses loopback addresses: initialization failed,
//   -97), whose connections are the loader's server;
// - its curl requests (ALL.Net, Mucha) are answered in the process, at
//   curl_easy_perform: the host's libcurl (OpenSSL 3) would not speak the
//   TLS 1.0 of a server on the game's OpenSSL 0.9.8;
// - its game server client (its own HTTPS over OpenSSL, to the URI ALL.Net
//   gives it, https://localhost:9002) connects to a TLS server of the
//   loader's on a port of its own, which uses the game's OpenSSL with a
//   certificate of its own: the game's check of the server's certificate is
//   off for its contexts (SSL_CTX_set_verify).

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "namcoEs1Wmmt4Server.h"

#define SERVER_ADDRESS WMMT4_SERVER_ADDRESS
#include "../common/importHook.h"
#include "../log/log.h"

int wmmt4ServerTrace;

// The services' names, as the game dials them.
static int serverName(const char *name)
{
    static const char *const names[] = {"naominet.jp", "v388-front.mucha-prd.nbgi-amnet.jp",
                                        "v388-front.mucha-prd.bandainamcogames.cn", "mucha.local", NULL};
    size_t n;

    if (!name)
        return 0;
    for (const char *const *s = names; *s; s++)
        if (!strcasecmp(name, *s))
            return 1;
    n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".loc"); // nbgi.loc, tenporouter.loc, bbrouter.loc
}

// ---------------------------------------------------------------------------
// Names and connections.

static int (*realGetaddrinfo)(const char *, const char *, const struct addrinfo *, struct addrinfo **);
static struct hostent *(*realGethostbyname)(const char *);
static int (*realConnect)(int, const struct sockaddr *, socklen_t);
static uint16_t tlsPort, httpPort;

__attribute__((force_align_arg_pointer)) static int gameGetaddrinfo(const char *node, const char *service,
                                                                    const struct addrinfo *hints,
                                                                    struct addrinfo **result)
{
    int local = serverName(node);
    int r = realGetaddrinfo(local ? SERVER_ADDRESS : node, service, hints, result);
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: getaddrinfo(%s, %s)%s = %d\n", node ? node : "-", service ? service : "-",
                local ? " -> " SERVER_ADDRESS : "", r);
    return r;
}

__attribute__((force_align_arg_pointer)) static struct hostent *gameGethostbyname(const char *name)
{
    int local = serverName(name);
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: gethostbyname(%s)%s\n", name ? name : "-", local ? " -> " SERVER_ADDRESS : "");
    return realGethostbyname(local ? SERVER_ADDRESS : name);
}

// The services' address, and the game server's ports on this machine (443
// of the URI the game has before ALL.Net answers), are the loader's TLS
// server.
__attribute__((force_align_arg_pointer)) static int gameConnect(int fd, const struct sockaddr *address,
                                                                socklen_t length)
{
    if (address && address->sa_family == AF_INET && length >= sizeof(struct sockaddr_in) && tlsPort)
    {
        struct sockaddr_in to = *(const struct sockaddr_in *)address;
        uint16_t port = ntohs(to.sin_port);
        int server = to.sin_addr.s_addr == inet_addr(SERVER_ADDRESS);
        if (server || (to.sin_addr.s_addr == htonl(INADDR_LOOPBACK) && (port == 443 || port == 9002 || port == 10082)))
        {
            if (wmmt4ServerTrace)
                fprintf(stderr, "WMMT4 server: connect %s:%u -> the loader's server\n",
                        server ? SERVER_ADDRESS : "127.0.0.1", port);
            to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            to.sin_port = htons(port == 80 ? httpPort : tlsPort);
            return realConnect(fd, (struct sockaddr *)&to, sizeof(to));
        }
    }
    return realConnect(fd, address, length);
}

// ---------------------------------------------------------------------------
// curl (ALL.Net, Mucha): the requests to the services answered in the
// process. A handle's options are noted as they are set.

#define CURLOPT_WRITEDATA 10001
#define CURLOPT_URL 10002
#define CURLOPT_POSTFIELDS 10015
#define CURLOPT_READDATA 10009
#define CURLOPT_HEADERDATA 10029
#define CURLOPT_READFUNCTION 20012
#define CURLOPT_COPYPOSTFIELDS 10165
#define CURLOPT_WRITEFUNCTION 20011
#define CURLOPT_HEADERFUNCTION 20079
#define CURLOPT_POSTFIELDSIZE 60
#define CURLINFO_RESPONSE_CODE 0x200002

typedef size_t (*CurlWrite)(const char *, size_t, size_t, void *);

typedef struct
{
    void *handle;
    char url[512];
    const uint8_t *post;
    uint8_t *postCopy;
    long postSize;
    CurlWrite write, header;
    void *writeData, *headerData;
    size_t (*read)(char *, size_t, size_t, void *);
    void *readData;
    long status;
} CurlState;

static CurlState curls[16];
static pthread_mutex_t curlLock = PTHREAD_MUTEX_INITIALIZER;

static CurlState *curlState(void *handle)
{
    CurlState *free_ = NULL;
    for (size_t i = 0; i < sizeof(curls) / sizeof(curls[0]); i++)
    {
        if (curls[i].handle == handle)
            return &curls[i];
        if (!curls[i].handle && !free_)
            free_ = &curls[i];
    }
    if (free_)
    {
        memset(free_, 0, sizeof(*free_));
        free_->handle = handle;
        free_->postSize = -1;
    }
    return free_;
}

static int (*realCurlSetopt)(void *, int, void *);
static int (*realCurlPerform)(void *);
static int (*realCurlGetinfo)(void *, int, void *);
static void (*realCurlCleanup)(void *);

__attribute__((force_align_arg_pointer)) static int gameCurlSetopt(void *handle, int option, void *value)
{
    pthread_mutex_lock(&curlLock);
    CurlState *c = curlState(handle);
    if (c)
    {
        switch (option)
        {
        case CURLOPT_URL:
            snprintf(c->url, sizeof(c->url), "%s", value ? (const char *)value : "");
            break;
        case CURLOPT_POSTFIELDS:
            c->post = value;
            break;
        case CURLOPT_COPYPOSTFIELDS:
            free(c->postCopy);
            c->postCopy = NULL;
            if (value)
            {
                size_t n = c->postSize >= 0 ? (size_t)c->postSize : strlen(value);
                if ((c->postCopy = malloc(n + 1)))
                {
                    memcpy(c->postCopy, value, n);
                    c->postCopy[n] = 0;
                }
            }
            c->post = c->postCopy;
            break;
        case CURLOPT_POSTFIELDSIZE:
            c->postSize = (long)value;
            break;
        case CURLOPT_WRITEFUNCTION:
            c->write = (CurlWrite)value;
            break;
        case CURLOPT_WRITEDATA:
            c->writeData = value;
            break;
        case CURLOPT_HEADERFUNCTION:
            c->header = (CurlWrite)value;
            break;
        case CURLOPT_HEADERDATA:
            c->headerData = value;
            break;
        case CURLOPT_READFUNCTION:
            *(void **)&c->read = value;
            break;
        case CURLOPT_READDATA:
            c->readData = value;
            break;
        }
    }
    pthread_mutex_unlock(&curlLock);
    return realCurlSetopt(handle, option, value);
}

// "scheme://host[:port]/path": the host and the path.
static int splitUrl(const char *url, char *host, size_t hostSize, const char **path)
{
    const char *p = strstr(url, "://");
    if (!p)
        return 0;
    p += 3;
    size_t n = strcspn(p, ":/");
    if (n >= hostSize)
        return 0;
    memcpy(host, p, n);
    host[n] = 0;
    *path = strchr(p, '/');
    if (!*path)
        *path = "/";
    return 1;
}

__attribute__((force_align_arg_pointer)) static int gameCurlPerform(void *handle)
{
    char host[256], url[512];
    const char *path;
    const uint8_t *post;
    long postSize;
    CurlWrite write, header;
    void *writeData, *headerData;

    pthread_mutex_lock(&curlLock);
    CurlState *c = curlState(handle);
    if (!c || !splitUrl(c->url, host, sizeof(host), &path) || (!serverName(host) && *host))
    {
        pthread_mutex_unlock(&curlLock);
        return realCurlPerform(handle);
    }
    snprintf(url, sizeof(url), "%s", c->url);
    splitUrl(url, host, sizeof(host), &path);
    post = c->post;
    postSize = c->postSize >= 0 ? c->postSize : (post ? (long)strlen((const char *)post) : 0);
    write = c->write;
    writeData = c->writeData;
    header = c->header;
    headerData = c->headerData;
    size_t (*read)(char *, size_t, size_t, void *) = c->read;
    void *readData = c->readData;
    pthread_mutex_unlock(&curlLock);

    // A body given through a read function (Mucha's).
    uint8_t *readBody = NULL;
    if (!post && read)
    {
        size_t size = 0, cap = 4096;
        readBody = malloc(cap + 1);
        for (size_t n; readBody && (n = read((char *)readBody + size, 1, cap - size, readData)) > 0;)
        {
            size += n;
            if (size == cap && !(readBody = realloc(readBody, (cap *= 2) + 1)))
                break;
        }
        if (readBody)
            readBody[size] = 0;
        post = readBody;
        postSize = (long)size;
    }
    if (!post)
        postSize = 0;

    Wmmt4Response response = {404, "text/plain", NULL, 0};
    wmmt4ServerHandle(host, path, post, (size_t)postSize, &response);
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: curl %s (%ld bytes) -> %d, %zu bytes\n", url, postSize, response.status,
                response.length);
    if (header)
    {
        char line[128];
        int n = snprintf(line, sizeof(line), "HTTP/1.1 %d OK\r\n", response.status);
        header(line, 1, n, headerData);
        n = snprintf(line, sizeof(line), "Content-Type: %s\r\n", response.contentType);
        header(line, 1, n, headerData);
        n = snprintf(line, sizeof(line), "Content-Length: %zu\r\n", response.length);
        header(line, 1, n, headerData);
        header("\r\n", 1, 2, headerData);
    }
    if (response.length)
    {
        if (write)
            write((const char *)response.body, 1, response.length, writeData);
        else if (writeData)
            fwrite(response.body, 1, response.length, writeData);
    }
    free(response.body);
    free(readBody);
    pthread_mutex_lock(&curlLock);
    if ((c = curlState(handle)))
        c->status = response.status;
    pthread_mutex_unlock(&curlLock);
    return 0; // CURLE_OK
}

__attribute__((force_align_arg_pointer)) static int gameCurlGetinfo(void *handle, int info, void *value)
{
    pthread_mutex_lock(&curlLock);
    CurlState *c = curlState(handle);
    long status = c ? c->status : 0;
    pthread_mutex_unlock(&curlLock);
    if (info == CURLINFO_RESPONSE_CODE && status && value)
    {
        *(long *)value = status;
        return 0;
    }
    return realCurlGetinfo(handle, info, value);
}

__attribute__((force_align_arg_pointer)) static void gameCurlCleanup(void *handle)
{
    pthread_mutex_lock(&curlLock);
    for (size_t i = 0; i < sizeof(curls) / sizeof(curls[0]); i++)
        if (curls[i].handle == handle)
        {
            free(curls[i].postCopy);
            memset(&curls[i], 0, sizeof(curls[i]));
        }
    pthread_mutex_unlock(&curlLock);
    realCurlCleanup(handle);
}

// ---------------------------------------------------------------------------
// The game server: HTTPS on the game's OpenSSL (0.9.8, already loaded).

static const char serverCertificate[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDXTCCAkWgAwIBAgIUTKDczxPab7dHqAjEhZBVl6lu8O0wDQYJKoZIhvcNAQEF\n"
    "BQAwPTESMBAGA1UEAwwJbG9jYWxob3N0MScwJQYDVQQKDB5saW51eGxvYWRlciBX\n"
    "TU1UNCBsb2NhbCBzZXJ2ZXIwIBcNMjYxMDI3MDAyODI4WhgPMjEyNjEwMDMwMDI4\n"
    "MjhaMD0xEjAQBgNVBAMMCWxvY2FsaG9zdDEnMCUGA1UECgwebGludXhsb2FkZXIg\n"
    "V01NVDQgbG9jYWwgc2VydmVyMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC\n"
    "AQEApYuBX+0CHhaALfTJAGNPsCMq2CCxwOJ5a2HaF2QpzOZrvSZvEdBZvUDdwwpO\n"
    "dXWkW2yuqyrrWsIzjUJ1pEe7v3/s19BJ1Yd+vzHxZfOmHlfT8yoG7Pb/TnYtfh8M\n"
    "rUeJVlGmBMyBU1HovEnmYfABqa8P5LnDufnYRFFleSN6VRlVa6INLQlOyl1EGqPy\n"
    "smSgHqr8oBjLYznbrsJP1y+vqwJ0TGaxw7fRbsK9eH4Puspy5us71FwVdUApqNLW\n"
    "lCa04xv3sluwTHlxCv3hZ3aO841BrZyHmeiYT+vBC4+0A21Lz81ii1BrR5NN4iLK\n"
    "XSq2vPLAGaLQAc6IhuACRu52PwIDAQABo1MwUTAdBgNVHQ4EFgQUH6ntNlSUuHWJ\n"
    "V+JPt05Or8rRDoQwHwYDVR0jBBgwFoAUH6ntNlSUuHWJV+JPt05Or8rRDoQwDwYD\n"
    "VR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQUFAAOCAQEAfwHd+Q6vL7RFHsjuzM4M\n"
    "AcLBvB8tTGh1++/AbwT5azPDKgh6rJuRscmcqlIRQDOrwRQfxfsBOxUMmqvfuC/i\n"
    "dBYxa3XFe/yvCp90qkHmALPhEWOXlrwLhsEXol3sbGH0k3FiVbX9KpxZH/27fiTb\n"
    "ZrV/+LNphE14LoRngWq5G3bvdRhEogNFqGdNiGGwiwD9SXou4Tvpp1g9Xi46ubqf\n"
    "ZaKLAhi0BtSPcWOPIzThOPR4biWwgHjBfM0LDsYYDEzgID0YfNo9bvXPjkQ5pSyK\n"
    "LRIQn+KCxsT0ox5knGE4jAYg6GeoYm+0NlFV0iqHsczy3pwUAXywoqZskthwUJM5\n"
    "0A==\n"
    "-----END CERTIFICATE-----\n"
    ;
static const char serverKey[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEowIBAAKCAQEApYuBX+0CHhaALfTJAGNPsCMq2CCxwOJ5a2HaF2QpzOZrvSZv\n"
    "EdBZvUDdwwpOdXWkW2yuqyrrWsIzjUJ1pEe7v3/s19BJ1Yd+vzHxZfOmHlfT8yoG\n"
    "7Pb/TnYtfh8MrUeJVlGmBMyBU1HovEnmYfABqa8P5LnDufnYRFFleSN6VRlVa6IN\n"
    "LQlOyl1EGqPysmSgHqr8oBjLYznbrsJP1y+vqwJ0TGaxw7fRbsK9eH4Puspy5us7\n"
    "1FwVdUApqNLWlCa04xv3sluwTHlxCv3hZ3aO841BrZyHmeiYT+vBC4+0A21Lz81i\n"
    "i1BrR5NN4iLKXSq2vPLAGaLQAc6IhuACRu52PwIDAQABAoIBABj6LwIqWu7vkIBz\n"
    "613lHhFihPozbfkUfU2/562EKIW+CxRwzO0AUCyXXGkApZeLBp+KtYyllE7PIq3A\n"
    "u3TsI9OVeXE91TNkAbIsVr1TYUWOaoVzr7QnjPF+2typfPqqDU/aUDUaJrXLvn9Q\n"
    "8R3myKwcT0bAkTD+wEGH5x+7bso8TWUSaCNUIzuZerCpq03bu4grUSXkhTHh2DAF\n"
    "Rb9l3/8GnoVLcesjxuvOD1/6WKoLFt4vmIQ6EIEldFH9AqjqKDN55W1WhBLD8zZU\n"
    "CXqoPZxtZQmRRwjUBzkcjROCienW012eebmbXM7DVmBA7odw6qEFRrEm+/Tkoczp\n"
    "KtH3jgkCgYEA4kMw25XnrJy5s+en3zNrtwNkQSJUNDKRBfRcLG1Wh/RxXby3UhHK\n"
    "i1jj1QqyRaOYwSIjNt+dctbMxTzUu4qHHYHmdDCAOZqCqhDSHmRwj5oQBEW7sCmB\n"
    "nnRL2C58IsbUQ00cJzRqTqh2ZR1tnmyt3GktABFtnoH18AlOo0a0LgcCgYEAu01q\n"
    "5I7/zqeYPfdMzPJAzM2eyHlI4SeY30a2b7fscPYmAOX1X6MBTm7EafmM9vxkUt0M\n"
    "pJhxLA1o9+GoAZKnHiyns7UeuhmT3NEEdBhyFWhMQf5XED8g55fyVkegSrd/cdxb\n"
    "Lu+sz9WRW1TesgiAbHz7f/qZKJ0kYMDoSO9qaAkCgYAHU3mfgokP07tHkfajiXyx\n"
    "XlC+syfa0NBVSwY5AglNOaL6EAQ3ig/u1x/5Y/FFw/hMOJi/TBpC/6V9tWyF7gRV\n"
    "9kDJ/lAIjXIxo33KMiJa4/gky0U6fP2PbeCOzWKDlcCFtVd2BYJzrYy31W4ZNRek\n"
    "DVvBps7P1RFrhkGfk8h9vQKBgF4nHISoHY8UUENV/RVI8aoB8r9zuF72OLKEKVEf\n"
    "c4uVMKVi82WP2yhCJhnfiCzf0s6Ur7ZNqxYp54oztPCVd8yDRgngEgIauWlW6JYz\n"
    "c6WvOCY0ZC+WjEPe7cLRXpIwi7AAxRVjBcuz3825a/iyrVvbt9Etd7AizhaU4i9H\n"
    "qoWBAoGBAKE8bO7/JwTmjae2QQt8CHawIf5McjVmc6Lp/OAJWKOCbpgNvyHMUH8r\n"
    "TKyV/eUStyon6vjJQGVbuhvuipU+VjkkJ2Ouu86zOVtCVhI7yiiHda6PVJc20FAX\n"
    "13JyVW/A99qIo6APhMgY15XpvN6o9ODRQeJ9p173aSdJkBBmXGOW\n"
    "-----END RSA PRIVATE KEY-----\n"
    ;

static struct
{
    void *(*serverMethod)(void);
    void *(*ctxNew)(void *);
    int (*useCertificate)(void *, void *);
    int (*usePrivateKey)(void *, void *);
    void *(*newBioMem)(const void *, int);
    void *(*readX509)(void *, void *, void *, void *);
    void *(*readKey)(void *, void *, void *, void *);
    int (*bioFree)(void *);
    void *(*sslNew)(void *);
    int (*setFd)(void *, int);
    int (*accept)(void *);
    int (*read)(void *, void *, int);
    int (*write)(void *, const void *, int);
    int (*shutdown)(void *);
    void (*free)(void *);
    void (*ctxSetVerify)(void *, int, void *);
} ssl;

static void *sslContext;

static int loadSsl(void)
{
    void *lib = dlopen("libssl.so.0.9.8", RTLD_NOW | RTLD_NOLOAD);
    void *crypto = dlopen("libcrypto.so.0.9.8", RTLD_NOW | RTLD_NOLOAD);

    if (!lib || !crypto)
        return 0;
    *(void **)&ssl.serverMethod = dlsym(lib, "SSLv23_server_method");
    *(void **)&ssl.ctxNew = dlsym(lib, "SSL_CTX_new");
    *(void **)&ssl.useCertificate = dlsym(lib, "SSL_CTX_use_certificate");
    *(void **)&ssl.usePrivateKey = dlsym(lib, "SSL_CTX_use_PrivateKey");
    *(void **)&ssl.sslNew = dlsym(lib, "SSL_new");
    *(void **)&ssl.setFd = dlsym(lib, "SSL_set_fd");
    *(void **)&ssl.accept = dlsym(lib, "SSL_accept");
    *(void **)&ssl.read = dlsym(lib, "SSL_read");
    *(void **)&ssl.write = dlsym(lib, "SSL_write");
    *(void **)&ssl.shutdown = dlsym(lib, "SSL_shutdown");
    *(void **)&ssl.free = dlsym(lib, "SSL_free");
    *(void **)&ssl.ctxSetVerify = dlsym(lib, "SSL_CTX_set_verify");
    *(void **)&ssl.newBioMem = dlsym(crypto, "BIO_new_mem_buf");
    *(void **)&ssl.readX509 = dlsym(crypto, "PEM_read_bio_X509");
    *(void **)&ssl.readKey = dlsym(crypto, "PEM_read_bio_PrivateKey");
    *(void **)&ssl.bioFree = dlsym(crypto, "BIO_free");
    for (void **p = (void **)&ssl; p < (void **)(&ssl + 1); p++)
        if (!*p)
            return 0;

    // The game has initialised the library (SSL_library_init) by now.
    if (!(sslContext = ssl.ctxNew(ssl.serverMethod())))
        return 0;
    void *bio = ssl.newBioMem(serverCertificate, -1);
    void *certificate = ssl.readX509(bio, NULL, NULL, NULL);
    ssl.bioFree(bio);
    bio = ssl.newBioMem(serverKey, -1);
    void *key = ssl.readKey(bio, NULL, NULL, NULL);
    ssl.bioFree(bio);
    return certificate && key && ssl.useCertificate(sslContext, certificate) == 1 &&
           ssl.usePrivateKey(sslContext, key) == 1;
}

// The game's check of the server's certificate: off (the loader's server
// is self-signed).
static void (*realCtxSetVerify)(void *, int, void *);

__attribute__((force_align_arg_pointer)) static void gameCtxSetVerify(void *context, int mode, void *callback)
{
    (void)mode;
    (void)callback;
    realCtxSetVerify(context, 0, NULL); // SSL_VERIFY_NONE
}

// A connection: TLS (s set) or plain (fd).
typedef struct
{
    void *s;
    int fd;
} Connection;

static int connRead(Connection *c, void *buffer, int size)
{
    return c->s ? ssl.read(c->s, buffer, size) : (int)recv(c->fd, buffer, size, 0);
}

static int connWrite(Connection *c, const void *buffer, int size)
{
    return c->s ? ssl.write(c->s, buffer, size) : (int)send(c->fd, buffer, size, MSG_NOSIGNAL);
}

static int readAll(Connection *s, char *buffer, int size, int *have)
{
    int n = connRead(s, buffer + *have, size - *have);
    if (n <= 0)
        return 0;
    *have += n;
    return 1;
}

// One HTTP request on a connection, and its answer.
static int serveRequest(Connection *s)
{
    static const int max = 1 << 22;
    char *buffer = malloc(max + 1), *end = NULL;
    int have = 0;
    long length = 0;

    if (!buffer)
        return 0;
    while (!(end = (have ? strstr(buffer, "\r\n\r\n") : NULL)))
    {
        if (have >= max || !readAll(s, buffer, max, &have))
        {
            free(buffer);
            return 0;
        }
        buffer[have] = 0;
    }
    *end = 0;
    char method[16] = "", path[512] = "", host[256] = "";
    sscanf(buffer, "%15s %511s", method, path);
    for (char *line = strstr(buffer, "\r\n"); line; line = strstr(line + 2, "\r\n"))
    {
        if (!strncasecmp(line + 2, "Content-Length:", 15))
            length = strtol(line + 17, NULL, 10);
        else if (!strncasecmp(line + 2, "Host:", 5))
            sscanf(line + 7, " %255[^:\r]", host);
    }
    int headerSize = (int)(end - buffer) + 4;
    if (length < 0 || headerSize + length > max)
    {
        free(buffer);
        return 0;
    }
    while (have < headerSize + length)
        if (!readAll(s, buffer, headerSize + length, &have))
        {
            free(buffer);
            return 0;
        }

    Wmmt4Response response = {404, "text/plain", NULL, 0};
    wmmt4ServerHandle(host, path, (uint8_t *)buffer + headerSize, (size_t)length, &response);
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: %s %s (%ld bytes) -> %d, %zu bytes\n", method, path, length, response.status,
                response.length);
    char head[256];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                     response.status, response.status == 200 ? "OK" : "Not Found", response.contentType,
                     response.length);
    connWrite(s, head, n);
    for (size_t sent = 0; sent < response.length;)
    {
        int w = connWrite(s, response.body + sent, (int)(response.length - sent));
        if (w <= 0)
            break;
        sent += w;
    }
    free(response.body);
    free(buffer);
    return 1;
}

// A connection's thread: arg is the socket, its low bit set for plain HTTP
// (fds are shifted).
static void *connectionThread(void *arg)
{
    intptr_t v = (intptr_t)arg;
    Connection c = {NULL, (int)(v >> 1)};

    if (v & 1)
        serveRequest(&c);
    else if ((c.s = ssl.sslNew(sslContext)) && ssl.setFd(c.s, c.fd) == 1 && ssl.accept(c.s) == 1)
    {
        serveRequest(&c);
        ssl.shutdown(c.s);
    }
    else if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: TLS handshake failed\n");
    if (c.s)
        ssl.free(c.s);
    close(c.fd);
    return NULL;
}

static void *listenThread(void *arg)
{
    intptr_t v = (intptr_t)arg;
    int listener = (int)(v >> 1), plain = (int)(v & 1);

    for (;;)
    {
        int fd = accept(listener, NULL, NULL);
        pthread_t thread;
        if (fd < 0)
        {
            if (errno == EINTR)
                continue;
            log_error("WMMT4 server: accept failed");
            return NULL;
        }
        if (pthread_create(&thread, NULL, connectionThread, (void *)(intptr_t)((fd << 1) | plain)) == 0)
            pthread_detach(thread);
        else
            close(fd);
    }
    return NULL;
}

// A listener on a free port of 127.0.0.1 (plain HTTP or TLS): its port, 0
// on failure.
static uint16_t listenOn(int plain)
{
    struct sockaddr_in address = {0};
    socklen_t length = sizeof(address);
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    pthread_t thread;

    if (listener < 0)
        return 0;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listener, 16) != 0 ||
        getsockname(listener, (struct sockaddr *)&address, &length) != 0 ||
        pthread_create(&thread, NULL, listenThread, (void *)(intptr_t)((listener << 1) | plain)) != 0)
    {
        close(listener);
        return 0;
    }
    pthread_detach(thread);
    return ntohs(address.sin_port);
}

// The servers are started when the game initialises OpenSSL itself, early
// in main.
static int startServer(void)
{
    if (!(httpPort = listenOn(1)) || !(tlsPort = listenOn(0)))
        return 0;
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: HTTP on 127.0.0.1:%u, HTTPS on 127.0.0.1:%u\n", httpPort, tlsPort);
    return 1;
}

static int (*realSslLibraryInit)(void);

// SSL_library_init, the game's, before any of its connections.
__attribute__((force_align_arg_pointer)) static int gameSslLibraryInit(void)
{
    int r = realSslLibraryInit();
    if (!loadSsl() || !startServer())
        log_error("WMMT4: the game server could not be started");
    return r;
}

void namcoEs1Wmmt4NetInstall(void)
{
    wmmt4ServerTrace = getenv("WMMT4_SERVER_TRACE") != NULL;
    *(void **)&realGetaddrinfo = hookExecutableImport("getaddrinfo", (void *)gameGetaddrinfo);
    *(void **)&realGethostbyname = hookExecutableImport("gethostbyname", (void *)gameGethostbyname);
    *(void **)&realConnect = hookExecutableImport("connect", (void *)gameConnect);
    *(void **)&realCurlSetopt = hookExecutableImport("curl_easy_setopt", (void *)gameCurlSetopt);
    *(void **)&realCurlPerform = hookExecutableImport("curl_easy_perform", (void *)gameCurlPerform);
    *(void **)&realCurlGetinfo = hookExecutableImport("curl_easy_getinfo", (void *)gameCurlGetinfo);
    *(void **)&realCurlCleanup = hookExecutableImport("curl_easy_cleanup", (void *)gameCurlCleanup);
    *(void **)&realCtxSetVerify = hookExecutableImport("SSL_CTX_set_verify", (void *)gameCtxSetVerify);
    *(void **)&realSslLibraryInit = hookExecutableImport("SSL_library_init", (void *)gameSslLibraryInit);
}
