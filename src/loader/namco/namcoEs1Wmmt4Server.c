// Wangan Midnight Maximum Tune 4's online services, answered by the loader
// (see namcoEs1Wmmt4Server.h), after Bayshore MT4 (a server emulator for the
// game: github.com/FestoROG-XS/bayshore-mt4) and the game's own protocol
// (docs/wmmt4-proto/, from the descriptors in WMN4r).
//
// - ALL.Net (http://naominet.jp/sys/servlet/PowerOn): the cabinet's
//   authentication. The request is "key=value&..." deflated and base64'd;
//   the answer, plain "key=value&...", gives the shop and the game server's
//   URI.
// - Mucha (https://.../boardauth.do, updatacheck.do): Namco's board
//   service, "key=value" both ways.
// - The game server (https://<uri>/method/<rpc>): protobuf requests and
//   answers (namcoEs1Wmmt4Rpc.c).

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "namcoEs1Wmmt4Server.h"

#define SHOP_NAME "LINUXLOADER"
#define PLACE_ID "JPN0FFF0"
#define REGION_ID 1 // Hokkaido? ALL.Net's region number

// ---------------------------------------------------------------------------
// Text answers.

typedef struct
{
    char *text;
    size_t len, cap;
} Text;

static void textf(Text *t, const char *format, ...)
{
    va_list args;
    for (;;)
    {
        va_start(args, format);
        int n = vsnprintf(t->text ? t->text + t->len : NULL, t->text ? t->cap - t->len : 0, format, args);
        va_end(args);
        if (n < 0)
            return;
        if (t->text && t->len + n < t->cap)
        {
            t->len += n;
            return;
        }
        t->cap = (t->cap + n + 1) * 2;
        t->text = realloc(t->text, t->cap);
        if (!t->text)
            return;
    }
}

static void answerText(Wmmt4Response *response, Text *t)
{
    response->status = 200;
    response->contentType = "text/plain";
    response->body = (uint8_t *)t->text;
    response->length = t->len;
}

// The value of key in "key=value&key=value" (not escaped), or "".
static void formValue(const char *form, const char *key, char *value, size_t size)
{
    size_t n = strlen(key);
    value[0] = 0;
    for (const char *p = form; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL)
        if (!strncmp(p, key, n) && p[n] == '=')
        {
            size_t m = strcspn(p + n + 1, "&\r\n");
            if (m >= size)
                m = size - 1;
            memcpy(value, p + n + 1, m);
            value[m] = 0;
            return;
        }
}

// ---------------------------------------------------------------------------
// ALL.Net.

static size_t base64Decode(const uint8_t *in, size_t length, uint8_t *out)
{
    unsigned bits = 0, value = 0;
    size_t n = 0;
    for (size_t i = 0; i < length; i++)
    {
        int c = in[i], v;
        if (c >= 'A' && c <= 'Z')
            v = c - 'A';
        else if (c >= 'a' && c <= 'z')
            v = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            v = c - '0' + 52;
        else if (c == '+' || c == '-')
            v = 62;
        else if (c == '/' || c == '_')
            v = 63;
        else
            continue;
        value = value << 6 | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out[n++] = (uint8_t)(value >> bits);
        }
    }
    return n;
}

// zlib's inflate, the game's (it links zlib and exports it): its z_stream
// (zlib's stable ABI) declared here, no zlib headers needed.
typedef struct
{
    const uint8_t *next_in;
    unsigned avail_in;
    unsigned long total_in;
    uint8_t *next_out;
    unsigned avail_out;
    unsigned long total_out;
    const char *msg;
    void *state, *zalloc, *zfree, *opaque;
    int data_type;
    unsigned long adler, reserved;
} ZStream;

#define Z_OK 0
#define Z_FINISH 4

static size_t inflateData(const uint8_t *in, size_t length, char *out, size_t size)
{
    int (*init)(ZStream *, int, const char *, int) = dlsym(RTLD_DEFAULT, "inflateInit2_");
    int (*run)(ZStream *, int) = dlsym(RTLD_DEFAULT, "inflate");
    int (*end)(ZStream *) = dlsym(RTLD_DEFAULT, "inflateEnd");
    ZStream z = {0};

    if (!init || !run || !end || init(&z, 15 + 32, "1.2.3", sizeof(z)) != Z_OK)
        return 0;
    z.next_in = in;
    z.avail_in = (unsigned)length;
    z.next_out = (uint8_t *)out;
    z.avail_out = (unsigned)size - 1;
    run(&z, Z_FINISH);
    size_t n = size - 1 - z.avail_out;
    end(&z);
    out[n] = 0;
    return n;
}

static void allnetPowerOn(const uint8_t *body, size_t length, Wmmt4Response *response)
{
    uint8_t *raw = malloc(length + 1);
    char form[4096] = "", token[128], gameId[32];
    Text t = {0};
    time_t now = time(NULL);
    struct tm local;

    if (raw)
    {
        size_t n = base64Decode(body, length, raw);
        if (!inflateData(raw, n, form, sizeof(form)))
            snprintf(form, sizeof(form), "%.*s", (int)length, (const char *)body); // not packed
        free(raw);
    }
    formValue(form, "token", token, sizeof(token));
    formValue(form, "game_id", gameId, sizeof(gameId));
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: ALL.Net PowerOn: %s\n", form);
    localtime_r(&now, &local);
    textf(&t,
          "stat=1&uri=https://" WMMT4_SERVER_ADDRESS ":9002&host=" WMMT4_SERVER_ADDRESS ":9002&place_id=" PLACE_ID
          "&name=" SHOP_NAME "&nickname=" SHOP_NAME "&region0=%d&region_name0=Hokkaido&region_name1=X"
          "&region_name2=Y&region_name3=Z&country=JPN&allnet_id=456&client_timezone=+0900&utc_time=%04d-%02d-%02dT"
          "%02d:%02d:%02dZ&setting=&res_ver=3&token=%s&timezone=+09:00&year=%d&month=%d&day=%d&hour=%d"
          "&minute=%d&second=%d&res_class=PowerOnResponseVer2\n",
          REGION_ID, local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
          local.tm_sec, token, local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
          local.tm_sec);
    answerText(response, &t);
}

// ---------------------------------------------------------------------------
// Mucha: the board's authentication (the shop, the URLs of its other
// services), the update check (no update), and the reports (taken).

#define MUCHA_URL "https://v388-front.mucha-prd.nbgi-amnet.jp:10082"

static void muchaBoardAuth(const char *form, Wmmt4Response *response)
{
    char placeId[64];
    Text t = {0};
    time_t now = time(NULL);
    struct tm local, utc;

    formValue(form, "placeId", placeId, sizeof(placeId));
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
    textf(&t,
          "RESULTS=001&AREA_0=008&AREA_0_EN=&AREA_1=009&AREA_1_EN=&AREA_2=010&AREA_2_EN=&AREA_3=011&AREA_3_EN="
          "&AREA_FULL_0=&AREA_FULL_0_EN=&AREA_FULL_1=&AREA_FULL_1_EN=&AREA_FULL_2=&AREA_FULL_2_EN=&AREA_FULL_3="
          "&AREA_FULL_3_EN=&AUTH_INTERVAL=86400&CHARGE_URL=" MUCHA_URL "/charge/&CONSUME_TOKEN=0&COUNTRY_CD=JPN"
          "&DONGLE_FLG=1&EXPIRATION_DATE=null&FILE_URL=" MUCHA_URL "/file/&FORCE_BOOT=0&PLACE_ID=%s"
          "&PREFECTURE_ID=14&SERVER_TIME=%04d%02d%02d%02d%02d&UTC_SERVER_TIME=%04d%02d%02d%02d%02d"
          "&SHOP_NAME=" SHOP_NAME "&SHOP_NAME_EN=" SHOP_NAME "&SHOP_NICKNAME=" SHOP_NAME "&SHOP_NICKNAME_EN=" SHOP_NAME
          "&URL_1=" MUCHA_URL "/url1/&URL_2=" MUCHA_URL "/url2/&URL_3=" MUCHA_URL "/url3/&USE_TOKEN=0",
          placeId[0] ? placeId : PLACE_ID, local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
          local.tm_min, utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min);
    answerText(response, &t);
}

static void muchaUpdateCheck(const char *form, Wmmt4Response *response)
{
    char version[32];
    Text t = {0};

    formValue(form, "gameVer", version, sizeof(version));
    textf(&t,
          "RESULTS=001&UPDATE_VER_1=%s&UPDATE_URL_1=" MUCHA_URL "/updUrl1/&UPDATE_SIZE_1=9318000"
          "&UPDATE_CRC_1=55C4000000000000&CHECK_URL_1=" MUCHA_URL "/checkUrl/&EXE_VER_1=%s&INFO_SIZE_1=180"
          "&COM_SIZE_1=16384&COM_TIME_1=100&LAN_INFO_SIZE_1=180",
          version, version);
    answerText(response, &t);
}

static void mucha(const char *path, const uint8_t *body, size_t length, Wmmt4Response *response)
{
    char *form = malloc(length + 1);
    Text t = {0};

    if (!form)
        return;
    if (!body)
        length = 0;
    memcpy(form, body, length);
    form[length] = 0;
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: Mucha %s: %s\n", path, form);
    if (strstr(path, "boardauth.do"))
        muchaBoardAuth(form, response);
    else if (strstr(path, "updatacheck.do"))
        muchaUpdateCheck(form, response);
    else
    {
        textf(&t, "RESULTS=001");
        answerText(response, &t);
    }
    free(form);
}

// ---------------------------------------------------------------------------

void wmmt4ServerHandle(const char *host, const char *path, const uint8_t *body, size_t length,
                       Wmmt4Response *response)
{
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: request %s%s, %zu bytes\n", host, path, length);
    response->status = 404;
    response->contentType = "text/plain";
    if (!strncmp(path, "/sys/servlet/PowerOn", 20))
        allnetPowerOn(body, length, response);
    else if (strstr(path, ".do"))
        mucha(path, body, length, response);
    else if (!strncmp(path, "/method/", 8) || !strncmp(path, "/resource/", 10))
        wmmt4RpcHandle(path, body, length, response);
}
