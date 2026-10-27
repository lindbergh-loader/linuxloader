// WMMT4's game server: its protobuf RPCs (POST /method/<name>) and resources
// (GET /resource/<name>), after Bayshore MT4's handlers, kept in files of the
// game's folder (save/server/):
// - user_<id>.pb: a user (USER_* fields below): its card, tutorials, cars;
// - car_<id>.pb: a car as load_car answers it (LoadCarResponse), with the
//   loader's own fields from 100 on (CAR_*);
// - team_<id>.pb: a team (TEAM_* fields), its members the users with its id;
// - ta.pb: the time attack records (repeated TA_* messages);
// - ids.pb: the next user, car and team numbers.
// Field numbers are those of docs/wmmt4-proto/v388.proto.

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "namcoEs1Wmmt4Pb.h"
#include "namcoEs1Wmmt4Server.h"

#define DIRECTORY "save/server"
#define REGION_ID 13 // Tokyo
#define PLACE_ID "JPN0FFF0"
#define SHOP_NAME "LINUXLOADER"
#define FAR_FUTURE 4000000000u

// ErrorCode
#define ERR_SUCCESS 0
#define ERR_NOT_FOUND 2
#define ERR_NAME_CONFLICTED 7
#define ERR_FORBIDDEN 9

// The loader's records.
#define USER_ID 1
#define USER_CHIP 2
#define USER_ACCESS_CODE 3
#define USER_TUTORIALS 4
#define USER_HP600 5
#define USER_CAR 6 // repeated, the order the game shows them in
#define USER_TEAM 7
#define USER_TEAM_CAR 8
#define IDS_USER 1
#define IDS_CAR 2
#define IDS_TEAM 3
#define TEAM_ID 1
#define TEAM_NAME 2
#define TEAM_LEADER 3
#define TEAM_CREATED_AT 4
#define TEAM_STICKER_FONT 5
#define TEAM_CLOSED 6
#define TEAM_APPLICANT 7 // repeated: user ids
#define TEAM_MAX_MEMBERS 50
#define CAR_USER 100
#define CAR_GHOST_BATTLES 101
#define CAR_GHOST_WINS 102
#define TA_RECORD 1 // repeated, in ta.pb
#define TA_CAR 1
#define TA_COURSE 2
#define TA_MODEL 3
#define TA_TUNE_POWER 4
#define TA_TUNE_HANDLING 5
#define TA_TIME 6
#define TA_SECTION 7 // to 13

// Car
#define C_CAR_ID 1
#define C_REGION_ID 2
#define C_NAME 3
#define C_SEARCH_CODE 4
#define C_MODEL 6
#define C_VISUAL_MODEL 7
#define C_DEFAULT_COLOR 8
#define C_CUSTOM_COLOR 9
#define C_TUNE_POWER 24
#define C_TUNE_HANDLING 25
#define C_TITLE 26
#define C_LEVEL 27
#define C_TEAM_STICKER 28
#define C_TEAM_ID 29
#define C_TEAM_NAME 30
#define C_TEAM_STICKER_FONT 31
#define C_LAST_PLAYED_AT 32
#define C_LAST_PLAYED_PLACE 33
#define C_GHOST_LEVEL 35
#define C_COUNTRY 36

// LoadCarResponse
#define L_ERROR 1
#define L_CAR 2
#define L_TUNING_POINT 3
#define L_DRESSUP_POINT 4
#define L_DRESSUP_LEVEL 5
#define L_ODOMETER 6
#define L_PLAY_COUNT 7
#define L_EARNED_CUSTOM_COLOR 8
#define L_OWNED_CUSTOM_COLORS 9
#define L_OWNED_DRESSUP_PARTS 10
#define L_SETTING 11
#define L_VS_PLAY_COUNT 13
#define L_VS_BURST_COUNT 14
#define L_VS_STAR_COUNT 15
#define L_VS_STAR_COUNT_MAX 16
#define L_VS_COOL_OR_WILD 17
#define L_VS_SMOOTH_OR_ROUGH 18
#define L_RG_PLAY_COUNT 19
#define L_RG_WIN_COUNT 20
#define L_MAXI_COIN 21
#define L_RG_BLOCK 22
#define L_RG_PROGRESS 23
#define L_RG_CLEAR_COUNT 24
#define L_RG_CONSECUTIVE_LOSSES 25
#define L_ST_PLAY_COUNT 26
#define L_ST_CLEAR_BITS 27
#define L_ST_CLEAR_DIV_COUNT 28
#define L_ST_CLEAR_COUNT 29
#define L_ST_LOSE_BITS 30
#define L_ST_LOSE 31
#define L_ST_CONSECUTIVE_WINS 32
#define L_ST_CONSECUTIVE_WINS_MAX 33
#define L_OWNED_METERS 34
#define L_OWNED_BGM 35
#define L_OWNED_NAMEPLATES 41

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// ---------------------------------------------------------------------------
// Files.

static void path(char *buffer, size_t size, const char *format, unsigned id)
{
    char name[64];
    snprintf(name, sizeof(name), format, id);
    snprintf(buffer, size, DIRECTORY "/%s", name);
}

static int load(const char *file, PbMsg *msg)
{
    FILE *f = fopen(file, "rb");
    memset(msg, 0, sizeof(*msg));
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = n > 0 ? malloc(n) : NULL;
    int ok = n == 0 || (data && fread(data, 1, n, f) == (size_t)n && pbParse(msg, data, n));
    free(data);
    fclose(f);
    if (!ok)
        pbFree(msg);
    return ok;
}

static void save(const char *file, const PbMsg *msg)
{
    char temporary[512];
    size_t n;
    uint8_t *data = pbSerialize(msg, &n);

    mkdir("save", 0755);
    mkdir(DIRECTORY, 0755);
    snprintf(temporary, sizeof(temporary), "%s.tmp", file);
    FILE *f = fopen(temporary, "wb");
    if (f)
    {
        int ok = fwrite(data, 1, n, f) == n;
        ok &= fclose(f) == 0;
        if (ok)
            rename(temporary, file);
    }
    free(data);
}

static unsigned nextId(uint32_t field)
{
    PbMsg ids;
    load(DIRECTORY "/ids.pb", &ids);
    unsigned id = (unsigned)pbUint(&ids, field, 1);
    pbSetUint(&ids, field, id + 1);
    save(DIRECTORY "/ids.pb", &ids);
    pbFree(&ids);
    return id;
}

static int loadUser(unsigned id, PbMsg *user)
{
    char file[256];
    path(file, sizeof(file), "user_%u.pb", id);
    return load(file, user);
}

static void saveUser(const PbMsg *user)
{
    char file[256];
    path(file, sizeof(file), "user_%u.pb", (unsigned)pbUint(user, USER_ID, 0));
    save(file, user);
}

static int loadCar(unsigned id, PbMsg *car)
{
    char file[256];
    path(file, sizeof(file), "car_%u.pb", id);
    return load(file, car);
}

static void saveCar(unsigned id, const PbMsg *car)
{
    char file[256];
    path(file, sizeof(file), "car_%u.pb", id);
    save(file, car);
}

static int loadTeam(unsigned id, PbMsg *team)
{
    char file[256];
    path(file, sizeof(file), "team_%u.pb", id);
    return id && load(file, team);
}

static void saveTeam(const PbMsg *team)
{
    char file[256];
    path(file, sizeof(file), "team_%u.pb", (unsigned)pbUint(team, TEAM_ID, 0));
    save(file, team);
}

static void deleteTeam(unsigned id)
{
    char file[256];
    path(file, sizeof(file), "team_%u.pb", id);
    unlink(file);
}

// The ids of the records named <prefix>_<id>.pb (malloc'd, 0-terminated).
static unsigned *recordIds(const char *prefix)
{
    DIR *d = opendir(DIRECTORY);
    struct dirent *e;
    size_t n = 0, capacity = 16;
    unsigned *ids = calloc(capacity + 1, sizeof(*ids));
    char format[32];

    snprintf(format, sizeof(format), "%s_%%u.pb", prefix);
    while (d && ids && (e = readdir(d)))
    {
        unsigned id;
        if (sscanf(e->d_name, format, &id) != 1 || strstr(e->d_name, ".tmp") || !id)
            continue;
        if (n == capacity)
        {
            unsigned *more = realloc(ids, (capacity * 2 + 1) * sizeof(*ids));
            if (!more)
                break;
            ids = more;
            capacity *= 2;
        }
        ids[n++] = id;
        ids[n] = 0;
    }
    if (d)
        closedir(d);
    return ids;
}

// The user with this card, by its chip id: 0 when there is none.
static unsigned findUser(const char *chip, PbMsg *user)
{
    DIR *d = opendir(DIRECTORY);
    struct dirent *e;
    unsigned found = 0;
    char text[128];

    memset(user, 0, sizeof(*user));
    if (!d)
        return 0;
    while (!found && (e = readdir(d)))
    {
        unsigned id;
        PbMsg u;
        if (sscanf(e->d_name, "user_%u.pb", &id) != 1 || strstr(e->d_name, ".tmp") || !loadUser(id, &u))
            continue;
        if (!strcmp(pbString(&u, USER_CHIP, text, sizeof(text)), chip))
        {
            found = id;
            *user = u;
        }
        else
            pbFree(&u);
    }
    closedir(d);
    return found;
}

static unsigned createUser(const char *chip, const char *accessCode, PbMsg *user)
{
    unsigned id = nextId(IDS_USER);
    memset(user, 0, sizeof(*user));
    pbSetUint(user, USER_ID, id);
    pbSetString(user, USER_CHIP, chip);
    pbSetString(user, USER_ACCESS_CODE, accessCode);
    pbSetUint(user, USER_TUTORIALS, 0);
    pbSetUint(user, USER_HP600, 0);
    saveUser(user);
    return id;
}

// A car first in the user's cars (added if it is not there).
static void carFirst(PbMsg *user, unsigned car)
{
    unsigned *cars = calloc(user->count + 1, sizeof(*cars));
    size_t n = 0;
    for (size_t i = 0; cars && i < user->count; i++)
        if (user->fields[i].field == USER_CAR && user->fields[i].value != car)
            cars[n++] = (unsigned)user->fields[i].value;
    pbRemove(user, USER_CAR);
    pbAddUint(user, USER_CAR, car);
    for (size_t i = 0; i < n; i++)
        pbAddUint(user, USER_CAR, cars[i]);
    free(cars);
}

// ---------------------------------------------------------------------------
// Answers.

static void answer(Wmmt4Response *response, const PbMsg *msg)
{
    response->status = 200;
    response->contentType = "application/x-protobuf; revision=5112";
    response->body = pbSerialize(msg, &response->length);
}

static void answerError(Wmmt4Response *response, unsigned error)
{
    PbMsg r = {0};
    pbSetUint(&r, 1, error);
    answer(response, &r);
    pbFree(&r);
}

// A car with its owner's team (not stored: the team may change).
static void carTeam(PbMsg *car, unsigned userId)
{
    PbMsg user, team;
    pbRemove(car, C_TEAM_ID);
    pbRemove(car, C_TEAM_NAME);
    pbRemove(car, C_TEAM_STICKER_FONT);
    if (!loadUser(userId, &user))
        return;
    if (loadTeam((unsigned)pbUint(&user, USER_TEAM, 0), &team))
    {
        pbSetUint(car, C_TEAM_ID, pbUint(&team, TEAM_ID, 0));
        const PbField *name = pbGet(&team, TEAM_NAME);
        pbSetBytes(car, C_TEAM_NAME, name ? name->data : NULL, name ? name->length : 0);
        pbSetUint(car, C_TEAM_STICKER_FONT, pbUint(&team, TEAM_STICKER_FONT, 0));
        pbFree(&team);
    }
    pbFree(&user);
}

// A car as the game is given it (the stored one, the loader's fields off).
static void publicCar(const PbMsg *stored, PbMsg *car)
{
    pbSub(stored, L_CAR, car);
    carTeam(car, (unsigned)pbUint(stored, CAR_USER, 0));
}

// A user's car as the game is given it, by its id: 0 when there is none.
static int publicCarById(unsigned id, PbMsg *car)
{
    PbMsg stored;
    memset(car, 0, sizeof(*car));
    if (!loadCar(id, &stored))
        return 0;
    publicCar(&stored, car);
    pbFree(&stored);
    return 1;
}

// ---------------------------------------------------------------------------
// Startup.

static void registerSystemInfo(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg r = {0};
    (void)request;
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 2, REGION_ID);
    pbSetString(&r, 3, PLACE_ID);
    pbSetUint(&r, 4, 1300000000); // pajero_disclose_at: disclosed
    pbSetUint(&r, 5, 0);          // car_campaign_start_at
    pbSetUint(&r, 6, 0);          // car_campaign_end_at
    pbSetUint(&r, 7, FAR_FUTURE); // team_suspension_announcement_start_at
    pbSetUint(&r, 8, FAR_FUTURE); // team_suspension_start_at
    pbSetUint(&r, 9, FAR_FUTURE); // succession_close_announcement_start_at
    pbSetUint(&r, 10, FAR_FUTURE); // succession_close_at
    pbSetUint(&r, 11, FAR_FUTURE); // succession_close_announcement_end_at
    pbSetUint(&r, 12, 0);         // face_recognition_permitted
    pbSetUint(&r, 13, 1);         // latest_competition_id
    answer(response, &r);
    pbFree(&r);
}

static void ping(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg r = {0};
    (void)request;
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 2, 1);
    answer(response, &r);
    pbFree(&r);
}

// ---------------------------------------------------------------------------
// Users.

static void loadUserRpc(const PbMsg *request, Wmmt4Response *response)
{
    char chip[128], accessCode[64];
    PbMsg user, r = {0};
    unsigned id;
    unsigned cars = 0;

    pbString(request, 1, chip, sizeof(chip));
    pbString(request, 2, accessCode, sizeof(accessCode));
    int existing = (id = findUser(chip, &user)) != 0;
    if (!existing)
        id = createUser(chip, accessCode, &user);
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 2, 0); // unlock_at
    pbSetString(&r, 3, accessCode);
    pbSetUint(&r, 4, id); // banapassport_am_id
    pbSetUint(&r, 5, id); // personal_id
    pbSetUint(&r, 6, id); // user_id
    for (size_t i = 0; i < user.count; i++)
    {
        PbMsg stored, car, state = {0};
        if (user.fields[i].field != USER_CAR || !loadCar((unsigned)user.fields[i].value, &stored))
            continue;
        publicCar(&stored, &car);
        pbAddMsg(&r, 8, &car);
        pbSetUint(&state, 1, 0); // has_opponent_ghost
        pbSetUint(&state, 2, 1); // COMPETITION_NOT_PARTICIPATED
        pbAddMsg(&r, 9, &state);
        pbFree(&car);
        pbFree(&state);
        pbFree(&stored);
        cars++;
    }
    if (existing && !cars)
    {
        PbMsg state = {0};
        pbSetUint(&state, 1, 0);
        pbAddMsg(&r, 9, &state);
        pbFree(&state);
    }
    pbSetUint(&r, 7, cars);
    unsigned hp600 = (unsigned)pbUint(&user, USER_HP600, 0);
    pbSetUint(&r, 10, hp600 > 0);
    pbSetUint(&r, 11, hp600);
    pbSetUint(&r, 12, pbUint(&user, USER_TUTORIALS, 0));
    if (existing)
    {
        pbSetUint(&r, 13, 1); // CAR_CAMPAIGN_NOT_ACCEPTED
        pbSetUint(&r, 14, 1); // COMPETITION_NOT_PARTICIPATED
    }
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: user %u (card %s): %u cars%s\n", id, chip, cars, existing ? "" : ", new");
    answer(response, &r);
    pbFree(&r);
    pbFree(&user);
}

static void createUserRpc(const PbMsg *request, Wmmt4Response *response)
{
    char chip[128], accessCode[64];
    PbMsg user, r = {0};

    pbString(request, 2, chip, sizeof(chip));
    pbString(request, 3, accessCode, sizeof(accessCode));
    unsigned id = findUser(chip, &user);
    if (!id)
        id = createUser(chip, accessCode, &user);
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetString(&r, 2, accessCode);
    pbSetUint(&r, 3, id);
    pbSetUint(&r, 4, id);
    pbSetUint(&r, 5, id);
    answer(response, &r);
    pbFree(&r);
    pbFree(&user);
}

static void askAccessCode(const PbMsg *request, Wmmt4Response *response)
{
    char chip[128], accessCode[64];
    PbMsg user, r = {0};

    pbString(request, 1, chip, sizeof(chip));
    if (!findUser(chip, &user))
    {
        answerError(response, ERR_NOT_FOUND);
        return;
    }
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetString(&r, 2, pbString(&user, USER_ACCESS_CODE, accessCode, sizeof(accessCode)));
    answer(response, &r);
    pbFree(&r);
    pbFree(&user);
}

// ---------------------------------------------------------------------------
// Cars.

static void searchCode(char code[7])
{
    static const char characters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static int seeded;
    if (!seeded)
    {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        seeded = 1;
    }
    for (int i = 0; i < 6; i++)
        code[i] = characters[rand() % 36];
    code[6] = 0;
}

static void createCar(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg car, stored = {0}, setting = {0}, user, r = {0};
    char code[7];
    unsigned userId = (unsigned)pbUint(request, 1, pbUint(request, 2, 0));

    if (!loadUser(userId, &user))
    {
        answerError(response, ERR_NOT_FOUND);
        return;
    }
    unsigned id = nextId(IDS_CAR);
    searchCode(code);
    pbSub(request, 8, &car);
    pbSetUint(&car, C_CAR_ID, id);
    pbDefaultUint(&car, C_REGION_ID, REGION_ID);
    pbSetString(&car, C_SEARCH_CODE, code);
    for (uint32_t f = C_CUSTOM_COLOR; f <= C_TITLE; f++)
        pbDefaultUint(&car, f, 0);
    pbDefaultUint(&car, C_LEVEL, 46);
    pbDefaultUint(&car, C_GHOST_LEVEL, 1);
    if (!pbHas(&car, C_COUNTRY))
        pbSetString(&car, C_COUNTRY, "JPN");
    pbSetUint(&car, C_LAST_PLAYED_AT, (uint64_t)time(NULL));

    // The stored car: load_car's answer, all at 0.
    for (uint32_t f = L_TUNING_POINT; f <= L_OWNED_NAMEPLATES; f++)
        if (f != L_SETTING && f != 12 && f != L_RG_PROGRESS && (f < 36 || f > 40))
            pbSetUint(&stored, f, 0);
    pbSetUint(&stored, L_ERROR, ERR_SUCCESS);
    for (int i = 0; i < 50; i++) // the ghost battles' progress, by area
        pbAddUint(&stored, L_RG_PROGRESS, 0);
    pbSetUint(&setting, 2, 1); // view
    pbSetUint(&setting, 3, pbUint(request, 9, 0)); // transmission
    pbSetUint(&setting, 4, 0); // retire
    pbSetUint(&setting, 5, 0); // meter
    pbSetUint(&setting, 6, 2); // volume
    pbSetUint(&setting, 7, 0); // bgm
    pbSetUint(&setting, 8, 0); // nameplate
    pbSetUint(&setting, 9, 0); // nameplate_color
    pbSetMsg(&stored, L_SETTING, &setting);
    if (pbUint(request, 10, 0) == 2) // CAR_HP600: a car started at 600 HP
    {
        pbSetUint(&car, C_LEVEL, 45);
        pbSetUint(&car, C_TUNE_POWER, 10);
        pbSetUint(&car, C_TUNE_HANDLING, 10);
        pbSetUint(&car, C_GHOST_LEVEL, 4);
        pbSetUint(&stored, L_ST_PLAY_COUNT, 20);
        pbSetUint(&stored, L_ST_CLEAR_DIV_COUNT, 1);
        pbSetUint(&stored, L_ST_CLEAR_COUNT, 20);
        pbSetUint(&stored, L_ST_CONSECUTIVE_WINS, 20);
        pbSetUint(&stored, L_ST_CONSECUTIVE_WINS_MAX, 20);
    }
    pbSetMsg(&stored, L_CAR, &car);
    pbSetUint(&stored, CAR_USER, userId);
    saveCar(id, &stored);

    carFirst(&user, id);
    saveUser(&user);

    pbSetUint(&r, 1, ERR_SUCCESS);
    pbCopy(&r, request, 4); // access_code
    pbSetUint(&r, 3, userId);
    pbSetUint(&r, 4, userId);
    pbSetUint(&r, 5, userId);
    pbSetUint(&r, 6, id);
    pbSetString(&r, 7, code);
    pbSetUint(&r, 8, pbUint(&stored, L_ST_CLEAR_BITS, 0));
    pbSetUint(&r, 9, pbUint(&stored, L_ST_CLEAR_DIV_COUNT, 0));
    pbSetUint(&r, 10, pbUint(&stored, L_ST_CLEAR_COUNT, 0));
    pbSetUint(&r, 11, 0);
    pbSetUint(&r, 12, 0);
    pbSetUint(&r, 13, pbUint(&stored, L_ST_CONSECUTIVE_WINS, 0));
    pbSetUint(&r, 14, pbUint(&stored, L_ST_CONSECUTIVE_WINS_MAX, 0));
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: car %u (%s) created for user %u\n", id, code, userId);
    answer(response, &r);
    pbFree(&r);
    pbFree(&user);
    pbFree(&car);
    pbFree(&setting);
    pbFree(&stored);
}

static void loadCarRpc(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg stored;
    unsigned id = (unsigned)pbUint(request, 1, 0);

    if (!loadCar(id, &stored))
    {
        answerError(response, ERR_NOT_FOUND);
        return;
    }
    PbMsg car;
    publicCar(&stored, &car);
    pbSetMsg(&stored, L_CAR, &car);
    pbFree(&car);
    for (uint32_t f = 100; f < 110; f++)
        pbRemove(&stored, f);
    answer(response, &stored);
    pbFree(&stored);
}

// A car from the game (its customisation, tuning, level) into the stored
// one.
static void mergeCar(PbMsg *stored, const PbMsg *update)
{
    PbMsg car;
    pbSub(stored, L_CAR, &car);
    for (uint32_t f = C_CUSTOM_COLOR; f <= C_TEAM_STICKER; f++)
        pbCopy(&car, update, f);
    pbCopy(&car, update, 34); // aura
    pbCopy(&car, update, C_GHOST_LEVEL);
    pbSetUint(&car, C_LAST_PLAYED_AT, (uint64_t)time(NULL));
    pbSetMsg(stored, L_CAR, &car);
    pbFree(&car);
}

// A setting from the game (all its fields are required) into the stored car.
static void mergeSetting(PbMsg *stored, const PbMsg *request, uint32_t field)
{
    PbMsg setting;
    if (!pbSub(request, field, &setting))
        return;
    pbRemove(&setting, 1); // car_id
    pbSetMsg(stored, L_SETTING, &setting);
    pbFree(&setting);
}

static void updateCar(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg stored, car;
    unsigned id = (unsigned)pbUint(request, 1, 0);

    if (!loadCar(id, &stored))
    {
        answerError(response, ERR_NOT_FOUND);
        return;
    }
    if (pbSub(request, 2, &car))
    {
        mergeCar(&stored, &car);
        pbFree(&car);
    }
    if (pbUint(request, 4, 0) > 0)
        pbSetUint(&stored, L_OWNED_CUSTOM_COLORS, pbUint(request, 4, 0));
    mergeSetting(&stored, request, 5);
    saveCar(id, &stored);
    if (pbHas(request, 3)) // hp600_count
    {
        PbMsg user;
        if (loadUser((unsigned)pbUint(&stored, CAR_USER, 0), &user))
        {
            pbSetUint(&user, USER_HP600, pbUint(request, 3, 0));
            saveUser(&user);
            pbFree(&user);
        }
    }
    pbFree(&stored);
    answerError(response, ERR_SUCCESS);
}

// Fields of a result into the stored car: pairs of (result field, car field).
static void mergeFields(PbMsg *stored, const PbMsg *result, const uint32_t (*map)[2], size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        const PbField *f = pbGet(result, map[i][0]);
        if (!f)
            continue;
        pbRemove(stored, map[i][1]);
        if (f->wire == PB_BYTES)
            pbAddBytes(stored, map[i][1], f->data, f->length);
        else
            pbAddUint(stored, map[i][1], f->value);
    }
}

static void saveTimeAttack(unsigned carId, const PbMsg *stored, const PbMsg *result)
{
    PbMsg all, kept = {0}, car;
    unsigned course = (unsigned)pbUint(result, 2, 0), time = (unsigned)pbUint(result, 1, 0);
    int better = 1;

    load(DIRECTORY "/ta.pb", &all);
    pbSub(stored, L_CAR, &car);
    for (size_t i = 0; i < all.count; i++)
    {
        PbMsg record;
        if (all.fields[i].field != TA_RECORD || !pbParse(&record, all.fields[i].data, all.fields[i].length))
            continue;
        if (pbUint(&record, TA_CAR, 0) == carId && pbUint(&record, TA_COURSE, ~0u) == course)
        {
            if (pbUint(&record, TA_TIME, ~0u) <= time)
            {
                better = 0;
                pbAddMsg(&kept, TA_RECORD, &record);
            }
        }
        else
            pbAddMsg(&kept, TA_RECORD, &record);
        pbFree(&record);
    }
    if (better)
    {
        PbMsg record = {0};
        pbSetUint(&record, TA_CAR, carId);
        pbSetUint(&record, TA_COURSE, course);
        pbSetUint(&record, TA_MODEL, pbUint(&car, C_MODEL, 0));
        pbSetUint(&record, TA_TUNE_POWER, pbUint(&car, C_TUNE_POWER, 0));
        pbSetUint(&record, TA_TUNE_HANDLING, pbUint(&car, C_TUNE_HANDLING, 0));
        pbSetUint(&record, TA_TIME, time);
        for (uint32_t s = 0; s < 7; s++)
            if (pbHas(result, 3 + s))
                pbSetUint(&record, TA_SECTION + s, pbUint(result, 3 + s, 0));
        pbAddMsg(&kept, TA_RECORD, &record);
        pbFree(&record);
    }
    save(DIRECTORY "/ta.pb", &kept);
    pbFree(&all);
    pbFree(&kept);
    pbFree(&car);
}

static void saveGameResult(const PbMsg *request, Wmmt4Response *response)
{
    static const uint32_t story[][2] = {{1, L_ST_PLAY_COUNT},   {2, L_TUNING_POINT},  {3, L_ST_CLEAR_BITS},
                                        {4, L_ST_CLEAR_DIV_COUNT}, {5, L_ST_CLEAR_COUNT}, {6, L_ST_LOSE_BITS},
                                        {7, L_ST_LOSE},         {8, L_ST_CONSECUTIVE_WINS}, {9, L_OWNED_METERS},
                                        {10, L_OWNED_BGM}};
    static const uint32_t versus[][2] = {{4, L_VS_PLAY_COUNT}, {5, L_VS_BURST_COUNT}, {6, L_VS_STAR_COUNT},
                                         {7, L_VS_COOL_OR_WILD}, {8, L_VS_SMOOTH_OR_ROUGH}};
    static const uint32_t ghost[][2] = {{7, L_RG_PLAY_COUNT},  {8, L_RG_WIN_COUNT},   {9, L_MAXI_COIN},
                                        {10, L_DRESSUP_POINT}, {11, L_DRESSUP_LEVEL}, {12, L_OWNED_DRESSUP_PARTS},
                                        {13, L_RG_BLOCK},      {15, L_RG_CLEAR_COUNT}};
    PbMsg stored, sub, user;
    unsigned id = (unsigned)pbUint(request, 1, 0);

    if (!loadCar(id, &stored))
    {
        answerError(response, ERR_NOT_FOUND);
        return;
    }
    if (pbSub(request, 7, &sub))
    {
        mergeCar(&stored, &sub);
        pbFree(&sub);
    }
    mergeSetting(&stored, request, 8);
    if (pbUint(request, 9, 0) > pbUint(&stored, L_ODOMETER, 0))
        pbSetUint(&stored, L_ODOMETER, pbUint(request, 9, 0));
    pbSetUint(&stored, L_PLAY_COUNT, pbUint(request, 4, pbUint(&stored, L_PLAY_COUNT, 0)));
    if (pbHas(request, 12))
        pbSetUint(&stored, L_EARNED_CUSTOM_COLOR, pbUint(request, 12, 0));
    if (pbUint(request, 13, 0) > 0)
        pbSetUint(&stored, L_OWNED_CUSTOM_COLORS, pbUint(request, 13, 0));

    int finished = !pbUint(request, 5, 0) && !pbUint(request, 6, 0); // not retired, not timed up
    unsigned mode = (unsigned)pbUint(request, 2, 0);
    if (finished && mode == 1 && pbSub(request, 14, &sub)) // story
    {
        mergeFields(&stored, &sub, story, sizeof(story) / sizeof(story[0]));
        if (pbUint(&stored, L_ST_CONSECUTIVE_WINS, 0) > pbUint(&stored, L_ST_CONSECUTIVE_WINS_MAX, 0))
            pbSetUint(&stored, L_ST_CONSECUTIVE_WINS_MAX, pbUint(&stored, L_ST_CONSECUTIVE_WINS, 0));
        pbFree(&sub);
    }
    else if (finished && mode == 2 && pbSub(request, 15, &sub)) // time attack
    {
        saveTimeAttack(id, &stored, &sub);
        pbFree(&sub);
    }
    else if (finished && mode == 3 && pbSub(request, 16, &sub)) // versus
    {
        mergeFields(&stored, &sub, versus, sizeof(versus) / sizeof(versus[0]));
        if (pbUint(&stored, L_VS_STAR_COUNT, 0) > pbUint(&stored, L_VS_STAR_COUNT_MAX, 0))
            pbSetUint(&stored, L_VS_STAR_COUNT_MAX, pbUint(&stored, L_VS_STAR_COUNT, 0));
        pbFree(&sub);
    }
    else if (mode == 4 && pbSub(request, 17, &sub)) // ghost battle
    {
        mergeFields(&stored, &sub, ghost, sizeof(ghost) / sizeof(ghost[0]));
        int progress = 0;
        for (size_t i = 0; i < sub.count; i++)
            progress |= sub.fields[i].field == 14;
        if (progress)
        {
            pbRemove(&stored, L_RG_PROGRESS);
            for (size_t i = 0; i < sub.count; i++)
                if (sub.fields[i].field == 14 && sub.fields[i].wire == PB_VARINT)
                    pbAddUint(&stored, L_RG_PROGRESS, sub.fields[i].value);
                else if (sub.fields[i].field == 14)
                    pbAddBytes(&stored, L_RG_PROGRESS, sub.fields[i].data, sub.fields[i].length);
        }
        pbSetUint(&stored, CAR_GHOST_BATTLES, pbUint(&stored, CAR_GHOST_BATTLES, 0) + 1);
        if (pbSint(&sub, 1, 0) > 0)
            pbSetUint(&stored, CAR_GHOST_WINS, pbUint(&stored, CAR_GHOST_WINS, 0) + 1);
        pbFree(&sub);
    }
    saveCar(id, &stored);

    // The user: tutorials seen, 600 HP count, this car first.
    if (loadUser((unsigned)pbUint(&stored, CAR_USER, 0), &user))
    {
        if (pbUint(request, 10, 0) > pbUint(&user, USER_TUTORIALS, 0))
            pbSetUint(&user, USER_TUTORIALS, pbUint(request, 10, 0));
        pbSetUint(&user, USER_HP600, pbUint(request, 11, pbUint(&user, USER_HP600, 0)));
        carFirst(&user, id);
        saveUser(&user);
        pbFree(&user);
    }
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: car %u: game result saved (mode %u%s)\n", id, mode,
                finished ? "" : ", not finished");
    pbFree(&stored);
    answerError(response, ERR_SUCCESS);
}

// ---------------------------------------------------------------------------
// Time attack records.

typedef struct
{
    unsigned car, course, model, power, handling, time;
} TaRecord;

static size_t loadRecords(TaRecord **records)
{
    PbMsg all;
    size_t n = 0;
    load(DIRECTORY "/ta.pb", &all);
    *records = calloc(all.count + 1, sizeof(**records));
    for (size_t i = 0; *records && i < all.count; i++)
    {
        PbMsg r;
        if (all.fields[i].field != TA_RECORD || !pbParse(&r, all.fields[i].data, all.fields[i].length))
            continue;
        TaRecord t = {(unsigned)pbUint(&r, TA_CAR, 0),         (unsigned)pbUint(&r, TA_COURSE, 0),
                      (unsigned)pbUint(&r, TA_MODEL, 0),       (unsigned)pbUint(&r, TA_TUNE_POWER, 0),
                      (unsigned)pbUint(&r, TA_TUNE_HANDLING, 0), (unsigned)pbUint(&r, TA_TIME, 0)};
        (*records)[n++] = t;
        pbFree(&r);
    }
    pbFree(&all);
    return n;
}

static int byTime(const void *a, const void *b)
{
    unsigned x = ((const TaRecord *)a)->time, y = ((const TaRecord *)b)->time;
    return x < y ? -1 : x > y;
}

static void loadTimeAttackRecord(const PbMsg *request, Wmmt4Response *response)
{
    TaRecord *records;
    size_t n = loadRecords(&records);
    unsigned car = (unsigned)pbUint(request, 1, 0), model = (unsigned)pbUint(request, 2, 0),
             course = (unsigned)pbUint(request, 3, 0);
    PbMsg r = {0};
    int whole = 0, byModel = 0;

    qsort(records, n, sizeof(*records), byTime);
    pbSetUint(&r, 1, ERR_SUCCESS);
    for (size_t i = 0; i < n; i++)
        if (records[i].course == course && whole < 100)
            pbAddUint(&r, 2, records[i].time), whole++;
    for (; whole < 100; whole++)
        pbAddUint(&r, 2, 599999);
    for (size_t i = 0; i < n; i++)
        if (records[i].course == course && records[i].model == model && byModel < 100)
            pbAddUint(&r, 3, records[i].time), byModel++;
    for (; byModel < 100; byModel++)
        pbAddUint(&r, 3, 599999);
    free(records);

    PbMsg all;
    load(DIRECTORY "/ta.pb", &all);
    for (size_t i = 0; i < all.count; i++)
    {
        PbMsg t;
        if (all.fields[i].field != TA_RECORD || !pbParse(&t, all.fields[i].data, all.fields[i].length))
            continue;
        if (pbUint(&t, TA_CAR, 0) == car && pbUint(&t, TA_COURSE, ~0u) == course)
        {
            pbSetUint(&r, 4, pbUint(&t, TA_TIME, 0));
            for (uint32_t s = 0; s < 7; s++)
                if (pbHas(&t, TA_SECTION + s))
                    pbSetUint(&r, 5 + s, pbUint(&t, TA_SECTION + s, 0));
        }
        pbFree(&t);
    }
    pbFree(&all);
    answer(response, &r);
    pbFree(&r);
}

static void loadGameHistory(const PbMsg *request, Wmmt4Response *response)
{
    TaRecord *records;
    size_t n = loadRecords(&records);
    unsigned car = (unsigned)pbUint(request, 1, 0);
    PbMsg stored, carMsg = {0}, r = {0};

    loadCar(car, &stored);
    pbSub(&stored, L_CAR, &carMsg);
    unsigned model = (unsigned)pbUint(&carMsg, C_MODEL, 0);
    qsort(records, n, sizeof(*records), byTime);
    pbSetUint(&r, 1, ERR_SUCCESS);
    for (unsigned course = 0; course <= 18; course++)
    {
        PbMsg t = {0};
        unsigned whole = 0, models = 0, wholeRank = 0, modelRank = 0;
        const TaRecord *own = NULL;
        for (size_t i = 0; i < n; i++)
        {
            if (records[i].course != course)
                continue;
            whole++;
            if (records[i].model == model)
                models++;
            if (records[i].car == car)
            {
                own = &records[i];
                wholeRank = whole;
                modelRank = models;
            }
        }
        pbSetUint(&t, 1, course);
        if (own)
        {
            pbSetUint(&t, 2, own->time);
            pbSetUint(&t, 3, own->power);
            pbSetUint(&t, 4, own->handling);
            pbSetUint(&t, 5, wholeRank);
            pbSetUint(&t, 7, modelRank);
        }
        pbSetUint(&t, 6, whole);
        pbSetUint(&t, 8, models);
        pbAddMsg(&r, 2, &t);
        pbFree(&t);
    }
    pbSetUint(&r, 3, (uint64_t)time(NULL));
    pbSetUint(&r, 5, pbUint(&stored, CAR_GHOST_BATTLES, 0));
    pbSetUint(&r, 6, pbUint(&stored, CAR_GHOST_WINS, 0));
    free(records);
    pbFree(&stored);
    pbFree(&carMsg);
    answer(response, &r);
    pbFree(&r);
}

// ---------------------------------------------------------------------------
// Resources.

static void ranking(Wmmt4Response *response)
{
    TaRecord *records;
    size_t n = loadRecords(&records);
    PbMsg r = {0};

    qsort(records, n, sizeof(*records), byTime);
    for (unsigned course = 0; course <= 18; course++)
    {
        PbMsg list = {0};
        unsigned rank = 0;
        pbSetUint(&list, 1, course);
        for (size_t i = 0; i < n && rank < 20; i++)
        {
            PbMsg entry = {0}, stored, car = {0};
            char name[64];
            if (records[i].course != course || !loadCar(records[i].car, &stored))
                continue;
            pbSub(&stored, L_CAR, &car);
            pbSetUint(&entry, 1, ++rank);
            pbSetUint(&entry, 2, records[i].time);
            pbSetUint(&entry, 3, records[i].car);
            pbSetString(&entry, 4, pbString(&car, C_NAME, name, sizeof(name)));
            pbSetUint(&entry, 5, pbUint(&car, C_REGION_ID, REGION_ID));
            pbSetUint(&entry, 6, records[i].model);
            pbSetUint(&entry, 7, pbUint(&car, C_VISUAL_MODEL, 0));
            pbSetUint(&entry, 8, pbUint(&car, C_DEFAULT_COLOR, 0));
            pbSetUint(&entry, 9, records[i].power);
            pbSetUint(&entry, 10, records[i].handling);
            pbSetUint(&entry, 11, pbUint(&car, C_TITLE, 0));
            pbSetUint(&entry, 12, pbUint(&car, C_LEVEL, 0));
            pbAddMsg(&list, 2, &entry);
            pbFree(&entry);
            pbFree(&car);
            pbFree(&stored);
        }
        pbAddMsg(&r, 1, &list);
        pbFree(&list);
    }
    for (unsigned type = 100; type <= 101; type++)
    {
        PbMsg list = {0};
        pbSetUint(&list, 1, type);
        pbAddMsg(&r, 1, &list);
        pbFree(&list);
    }
    free(records);
    answer(response, &r);
    pbFree(&r);
}

static void placeList(Wmmt4Response *response)
{
    PbMsg r = {0}, place = {0};
    pbSetString(&place, 1, PLACE_ID);
    pbSetString(&place, 2, SHOP_NAME);
    pbSetUint(&place, 3, REGION_ID);
    pbSetString(&place, 4, "JPN");
    pbAddMsg(&r, 1, &place);
    answer(response, &r);
    pbFree(&place);
    pbFree(&r);
}

// The ghosts to race (ghost_summary, ghost_list): one per ghost level, non
// human, looking as the cars kept here, under ids of their own
// (GHOST_CAR_ID + level). The game races its default ghosts, but an empty
// list leaves levels without one (std::out_of_range, the game ends).
#define GHOST_CAR_ID 900000
#define GHOST_LEVELS 10

static void ghosts(const char *query, Wmmt4Response *response)
{
    unsigned *ids = recordIds("car"), area = 0, n = 0;
    const char *a = query ? strstr(query, "area=") : NULL;
    PbMsg r = {0};

    if (a && (a[-1] == '?' || a[-1] == '&'))
        area = (unsigned)strtoul(a + 5, NULL, 10);
    for (unsigned *id = ids; id && *id; id++)
        n++;
    for (unsigned level = 1; n && level <= GHOST_LEVELS; level++)
    {
        PbMsg car, ghost = {0};
        if (!publicCarById(ids[(level - 1) % n], &car))
            continue;
        pbSetUint(&car, C_CAR_ID, GHOST_CAR_ID + level);
        pbSetUint(&car, C_GHOST_LEVEL, level);
        pbRemove(&car, C_TEAM_ID);
        pbRemove(&car, C_TEAM_NAME);
        pbRemove(&car, C_TEAM_STICKER_FONT);
        pbSetMsg(&ghost, 1, &car);
        pbSetUint(&ghost, 2, area);
        pbSetUint(&ghost, 3, 0); // ramp
        pbSetUint(&ghost, 5, 1); // nonhuman
        pbSetUint(&ghost, 6, level);
        pbAddMsg(&r, 1, &ghost);
        pbFree(&ghost);
        pbFree(&car);
    }
    free(ids);
    answer(response, &r);
    pbFree(&r);
}

static void carSummary(Wmmt4Response *response)
{
    PbMsg r = {0};
    pbSetUint(&r, 1, 0); // hit_count
    answer(response, &r);
    pbFree(&r);
}

// ---------------------------------------------------------------------------
// Car succession: a Maximum Tune 3DX+ car brought over (Bayshore's way: its
// class, tuning, odometer and counts kept, the rest from the start).

static void prepareCarSuccession(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg user, car, stored = {0}, setting = {0}, r = {0};
    char code[7];
    unsigned userId = (unsigned)pbUint(request, 1, 0);

    if (!loadUser(userId, &user))
    {
        answerError(response, ERR_NOT_FOUND);
        return;
    }
    unsigned id = nextId(IDS_CAR);
    unsigned power = (unsigned)pbUint(request, 6, 0), handling = (unsigned)pbUint(request, 7, 0);
    unsigned wins = (unsigned)pbUint(request, 24, 0) / 10 * 10;
    unsigned coins = (unsigned)pbUint(request, 22, 0);
    if (power + handling > 32)
        power = handling = 16;
    searchCode(code);
    pbSub(request, 26, &car);
    pbSetUint(&car, C_CAR_ID, id);
    pbDefaultUint(&car, C_REGION_ID, REGION_ID);
    pbSetString(&car, C_SEARCH_CODE, code);
    for (uint32_t f = C_CUSTOM_COLOR; f <= C_TITLE; f++)
        pbDefaultUint(&car, f, 0);
    pbSetUint(&car, C_TUNE_POWER, power);
    pbSetUint(&car, C_TUNE_HANDLING, handling);
    pbSetUint(&car, C_LEVEL, 47 - (unsigned)pbUint(request, 10, 0));
    pbDefaultUint(&car, C_GHOST_LEVEL, 1);
    if (!pbHas(&car, C_COUNTRY))
        pbSetString(&car, C_COUNTRY, "JPN");
    pbSetUint(&car, C_LAST_PLAYED_AT, (uint64_t)time(NULL));

    for (uint32_t f = L_TUNING_POINT; f <= L_OWNED_NAMEPLATES; f++)
        if (f != L_SETTING && f != 12 && f != L_RG_PROGRESS && (f < 36 || f > 40))
            pbSetUint(&stored, f, 0);
    pbSetUint(&stored, L_ERROR, ERR_SUCCESS);
    for (int i = 0; i < 50; i++)
        pbAddUint(&stored, L_RG_PROGRESS, 0);
    pbSetUint(&stored, L_ODOMETER, pbUint(request, 11, 0));
    pbSetUint(&stored, L_OWNED_METERS, 9);
    pbSetUint(&stored, L_OWNED_BGM, 15);
    pbSetUint(&stored, L_OWNED_CUSTOM_COLORS, pbUint(&car, C_CUSTOM_COLOR, 0));
    pbSetUint(&stored, L_EARNED_CUSTOM_COLOR, pbUint(&car, C_CUSTOM_COLOR, 0) > 0);
    pbSetUint(&stored, L_VS_PLAY_COUNT, pbUint(request, 12, 0));
    pbSetUint(&stored, L_VS_STAR_COUNT, pbUint(request, 13, 0));
    pbSetUint(&stored, L_VS_STAR_COUNT_MAX, pbUint(request, 13, 0));
    pbSetUint(&stored, L_MAXI_COIN, coins);
    pbSetUint(&stored, L_RG_WIN_COUNT, coins);
    pbSetUint(&stored, L_RG_PLAY_COUNT, coins);
    pbSetUint(&stored, L_ST_LOSE, pbUint(request, 15, 0));
    pbSetUint(&stored, L_ST_CLEAR_DIV_COUNT, pbUint(request, 24, 0) / 20);
    pbSetUint(&stored, L_ST_CONSECUTIVE_WINS, wins);
    pbSetUint(&stored, L_ST_CONSECUTIVE_WINS_MAX, wins);
    pbSetUint(&stored, L_ST_PLAY_COUNT, wins);
    pbSetUint(&stored, L_ST_CLEAR_COUNT, wins);
    pbSetUint(&setting, 2, 1);
    pbSetUint(&setting, 3, 1); // transmission: manual
    for (uint32_t f = 4; f <= 9; f++)
        pbSetUint(&setting, f, f == 6 ? 2 : 0);
    pbSetMsg(&stored, L_SETTING, &setting);
    pbSetMsg(&stored, L_CAR, &car);
    pbSetUint(&stored, CAR_USER, userId);
    saveCar(id, &stored);
    carFirst(&user, id);
    saveUser(&user);

    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 2, id); // succession_id: the car
    pbSetString(&r, 3, code);
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: car %u (%s) brought over for user %u\n", id, code, userId);
    answer(response, &r);
    pbFree(&r);
    pbFree(&setting);
    pbFree(&stored);
    pbFree(&car);
    pbFree(&user);
}

static void succeedCar(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg r = {0};
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 2, pbUint(request, 1, 0));
    answer(response, &r);
    pbFree(&r);
}

static void saveTerminalResult(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg user;
    if (!loadUser((unsigned)pbUint(request, 1, 0), &user))
    {
        answerError(response, ERR_NOT_FOUND);
        return;
    }
    if (pbHas(request, 3))
    {
        pbRemove(&user, USER_CAR);
        for (size_t i = 0; i < request->count; i++)
            if (request->fields[i].field == 3 && request->fields[i].wire == PB_VARINT)
                pbAddUint(&user, USER_CAR, request->fields[i].value);
    }
    if (pbUint(request, 4, 0))
        pbSetUint(&user, USER_TEAM_CAR, pbUint(request, 4, 0));
    if (pbUint(request, 5, 0) > pbUint(&user, USER_TUTORIALS, 0))
        pbSetUint(&user, USER_TUTORIALS, pbUint(request, 5, 0));
    saveUser(&user);
    pbFree(&user);
    answerError(response, ERR_SUCCESS);
}

// ---------------------------------------------------------------------------
// Teams: one shop's, their members the users holding their id.

static unsigned userTeam(unsigned userId)
{
    PbMsg user;
    if (!loadUser(userId, &user))
        return 0;
    unsigned team = (unsigned)pbUint(&user, USER_TEAM, 0);
    pbFree(&user);
    return team;
}

static void setUserTeam(unsigned userId, unsigned team)
{
    PbMsg user;
    if (!loadUser(userId, &user))
        return;
    if (team)
    {
        pbSetUint(&user, USER_TEAM, team);
        const PbField *first = NULL;
        for (size_t i = 0; i < user.count && !first; i++)
            if (user.fields[i].field == USER_CAR)
                first = &user.fields[i];
        pbSetUint(&user, USER_TEAM_CAR, first ? first->value : 0);
    }
    else
    {
        pbRemove(&user, USER_TEAM);
        pbRemove(&user, USER_TEAM_CAR);
    }
    saveUser(&user);
    pbFree(&user);
}

// A user's team car (the first of its cars when it chose none): 0 if none.
static unsigned teamCar(const PbMsg *user)
{
    unsigned car = (unsigned)pbUint(user, USER_TEAM_CAR, 0);
    for (size_t i = 0; !car && i < user->count; i++)
        if (user->fields[i].field == USER_CAR)
            car = (unsigned)user->fields[i].value;
    return car;
}

static unsigned ownedCars(const PbMsg *user)
{
    unsigned n = 0;
    for (size_t i = 0; i < user->count; i++)
        n += user->fields[i].field == USER_CAR;
    return n;
}

// The members (users) of a team, as TeamMember messages added to msg's field
// when it is not 0; their number and their cars' number.
static unsigned teamMembers(unsigned team, PbMsg *msg, uint32_t field, unsigned *cars)
{
    unsigned *ids = recordIds("user"), n = 0;
    *cars = 0;
    for (unsigned *id = ids; id && *id; id++)
    {
        PbMsg user, member = {0}, car;
        if (!loadUser(*id, &user))
            continue;
        if (pbUint(&user, USER_TEAM, 0) == team)
        {
            n++;
            *cars += ownedCars(&user);
            if (field && publicCarById(teamCar(&user), &car))
            {
                pbSetUint(&member, 1, *id);
                pbSetMsg(&member, 2, &car);
                pbSetUint(&member, 3, ownedCars(&user));
                pbAddMsg(msg, field, &member);
                pbFree(&car);
            }
        }
        pbFree(&member);
        pbFree(&user);
    }
    free(ids);
    return n;
}

// The Team message of a stored team.
static void teamMessage(const PbMsg *team, PbMsg *out)
{
    PbMsg leader, car = {0}, place = {0};
    unsigned cars, members = teamMembers((unsigned)pbUint(team, TEAM_ID, 0), NULL, 0, &cars);
    char name[64] = "";

    memset(out, 0, sizeof(*out));
    if (loadUser((unsigned)pbUint(team, TEAM_LEADER, 0), &leader))
    {
        publicCarById(teamCar(&leader), &car);
        pbFree(&leader);
    }
    pbSetUint(out, 1, pbUint(team, TEAM_ID, 0));
    pbCopy(out, team, TEAM_NAME);
    pbSetUint(out, 3, members);
    pbSetUint(out, 4, cars);
    pbSetUint(out, 5, pbUint(team, TEAM_LEADER, 0));
    pbSetString(out, 6, pbString(&car, C_NAME, name, sizeof(name)));
    pbSetUint(out, 7, pbUint(&car, C_REGION_ID, REGION_ID));
    pbSetUint(out, 8, pbUint(team, TEAM_STICKER_FONT, 0));
    pbSetUint(out, 9, members >= TEAM_MAX_MEMBERS);
    pbSetUint(out, 10, pbUint(team, TEAM_CLOSED, 0));
    pbSetUint(out, 11, 0); // recruitment_suspended
    pbSetUint(out, 12, pbUint(team, TEAM_CREATED_AT, 0));
    pbSetString(&place, 1, PLACE_ID);
    pbSetString(&place, 2, SHOP_NAME);
    pbSetUint(&place, 3, REGION_ID);
    pbSetString(&place, 4, "JPN");
    pbSetMsg(out, 13, &place);
    pbFree(&place);
    pbFree(&car);
}

// The team named so: its id, 0 when there is none.
static unsigned teamNamed(const char *name)
{
    unsigned *ids = recordIds("team"), found = 0;
    char text[64];
    for (unsigned *id = ids; id && *id && !found; id++)
    {
        PbMsg team;
        if (!loadTeam(*id, &team))
            continue;
        if (!strcmp(pbString(&team, TEAM_NAME, text, sizeof(text)), name))
            found = *id;
        pbFree(&team);
    }
    free(ids);
    return found;
}

static void checkTeamName(const PbMsg *request, Wmmt4Response *response)
{
    char name[64];
    answerError(response, teamNamed(pbString(request, 1, name, sizeof(name))) ? ERR_NAME_CONFLICTED : ERR_SUCCESS);
}

static void createTeam(const PbMsg *request, Wmmt4Response *response)
{
    char name[64];
    PbMsg team = {0};
    unsigned userId = (unsigned)pbUint(request, 1, 0);

    pbString(request, 2, name, sizeof(name));
    if (teamNamed(name))
    {
        answerError(response, ERR_NAME_CONFLICTED);
        return;
    }
    unsigned id = nextId(IDS_TEAM);
    pbSetUint(&team, TEAM_ID, id);
    pbCopy(&team, request, 2);
    pbSetUint(&team, TEAM_LEADER, userId);
    pbSetUint(&team, TEAM_CREATED_AT, pbUint(request, 3, (uint64_t)time(NULL)));
    pbSetUint(&team, TEAM_STICKER_FONT, 0);
    pbSetUint(&team, TEAM_CLOSED, 0);
    saveTeam(&team);
    pbFree(&team);
    setUserTeam(userId, id);
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 server: team %u (%s) created by user %u\n", id, name, userId);
    answerError(response, ERR_SUCCESS);
}

static void searchTeam(const PbMsg *request, Wmmt4Response *response)
{
    unsigned *ids = recordIds("team");
    char name[64], text[64];
    PbMsg r = {0};

    pbString(request, 1, name, sizeof(name));
    pbSetUint(&r, 1, ERR_SUCCESS);
    for (unsigned *id = ids; id && *id; id++)
    {
        PbMsg team, msg;
        if (!loadTeam(*id, &team))
            continue;
        pbString(&team, TEAM_NAME, text, sizeof(text));
        if (!*name || strstr(text, name))
        {
            teamMessage(&team, &msg);
            pbAddMsg(&r, 2, &msg);
            if (*name && !strcmp(text, name))
                pbSetMsg(&r, 3, &msg);
            pbFree(&msg);
        }
        pbFree(&team);
    }
    free(ids);
    answer(response, &r);
    pbFree(&r);
}

// The applicants of a team (leader's view) into msg's field.
static void teamApplicants(const PbMsg *team, PbMsg *msg, uint32_t field)
{
    for (size_t i = 0; i < team->count; i++)
    {
        PbMsg user, car, applicant = {0};
        if (team->fields[i].field != TEAM_APPLICANT || !loadUser((unsigned)team->fields[i].value, &user))
            continue;
        if (publicCarById(teamCar(&user), &car))
        {
            pbSetUint(&applicant, 1, team->fields[i].value);
            pbSetMsg(&applicant, 2, &car);
            pbSetUint(&applicant, 3, ownedCars(&user));
            pbSetUint(&applicant, 4, 7); // expires_at: days left
            pbAddMsg(msg, field, &applicant);
            pbFree(&car);
        }
        pbFree(&applicant);
        pbFree(&user);
    }
}

static void loadTeamInformation(const PbMsg *request, Wmmt4Response *response)
{
    unsigned userId = (unsigned)pbUint(request, 1, 0);
    PbMsg user, team, msg, r = {0};

    pbSetUint(&r, 1, ERR_SUCCESS);
    if (loadUser(userId, &user))
    {
        if (loadTeam((unsigned)pbUint(&user, USER_TEAM, 0), &team))
        {
            teamMessage(&team, &msg);
            pbSetUint(&r, 2, pbUint(&team, TEAM_CREATED_AT, 0));
            pbSetUint(&r, 3, 0); // applying
            pbSetMsg(&r, 4, &msg);
            if (pbUint(&team, TEAM_LEADER, 0) == userId)
                teamApplicants(&team, &r, 7);
            pbSetUint(&r, 8, teamCar(&user));
            pbFree(&msg);
            pbFree(&team);
        }
        pbFree(&user);
    }
    answer(response, &r);
    pbFree(&r);
}

static void joinTeam(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg team;
    unsigned userId = (unsigned)pbUint(request, 1, 0);
    if (!loadTeam((unsigned)pbUint(request, 2, 0), &team))
    {
        answerError(response, ERR_NOT_FOUND);
        return;
    }
    int applied = 0;
    for (size_t i = 0; i < team.count; i++)
        applied |= team.fields[i].field == TEAM_APPLICANT && team.fields[i].value == userId;
    if (!applied)
        pbAddUint(&team, TEAM_APPLICANT, userId);
    saveTeam(&team);
    pbFree(&team);
    answerError(response, ERR_SUCCESS);
}

static void approveTeamApplicants(const PbMsg *request, Wmmt4Response *response)
{
    unsigned leader = (unsigned)pbUint(request, 1, 0), id = userTeam(leader);
    PbMsg team = {0}, kept = {0}, r = {0};

    if (!loadTeam(id, &team) || pbUint(&team, TEAM_LEADER, 0) != leader)
    {
        pbFree(&team);
        answerError(response, ERR_FORBIDDEN);
        return;
    }
    for (size_t i = 0; i < team.count; i++)
    {
        const PbField *f = &team.fields[i];
        int decided = 0;
        for (size_t j = 0; j < request->count; j++)
            if ((request->fields[j].field == 2 || request->fields[j].field == 3) &&
                request->fields[j].wire == PB_VARINT && request->fields[j].value == f->value)
            {
                decided = 1;
                if (f->field == TEAM_APPLICANT && request->fields[j].field == 2 && !userTeam((unsigned)f->value))
                    setUserTeam((unsigned)f->value, id);
            }
        if (f->field != TEAM_APPLICANT)
            pbCopy(&kept, &team, f->field);
        else if (!decided)
            pbAddUint(&kept, TEAM_APPLICANT, f->value);
    }
    saveTeam(&kept);
    unsigned cars, members = teamMembers(id, NULL, 0, &cars), applicants = 0;
    for (size_t i = 0; i < kept.count; i++)
        applicants += kept.fields[i].field == TEAM_APPLICANT;
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 2, members);
    pbSetUint(&r, 3, cars);
    pbSetUint(&r, 4, applicants);
    answer(response, &r);
    pbFree(&r);
    pbFree(&kept);
    pbFree(&team);
}

// A team without members is gone; one without its leader gets another.
static void teamAfterLeaving(unsigned id, unsigned userId)
{
    PbMsg team, members = {0};
    unsigned cars;
    if (!loadTeam(id, &team))
        return;
    if (!teamMembers(id, &members, 1, &cars))
        deleteTeam(id);
    else if (pbUint(&team, TEAM_LEADER, 0) == userId)
    {
        PbMsg first;
        if (pbSub(&members, 1, &first))
        {
            pbSetUint(&team, TEAM_LEADER, pbUint(&first, 1, 0));
            pbFree(&first);
        }
        saveTeam(&team);
    }
    pbFree(&members);
    pbFree(&team);
}

static void leaveTeam(const PbMsg *request, Wmmt4Response *response)
{
    unsigned userId = (unsigned)pbUint(request, 1, 0), id = userTeam(userId);
    setUserTeam(userId, 0);
    if (id)
        teamAfterLeaving(id, userId);
    answerError(response, ERR_SUCCESS);
}

static void dissolveTeam(const PbMsg *request, Wmmt4Response *response)
{
    unsigned userId = (unsigned)pbUint(request, 1, 0), id = userTeam(userId);
    PbMsg team = {0};
    if (!loadTeam(id, &team) || pbUint(&team, TEAM_LEADER, 0) != userId)
    {
        pbFree(&team);
        answerError(response, ERR_FORBIDDEN);
        return;
    }
    unsigned *ids = recordIds("user");
    for (unsigned *u = ids; u && *u; u++)
        if (userTeam(*u) == id)
            setUserTeam(*u, 0);
    free(ids);
    deleteTeam(id);
    pbFree(&team);
    answerError(response, ERR_SUCCESS);
}

static void loadTeamMembers(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg team, msg, r = {0};
    unsigned cars;
    if (!loadTeam((unsigned)pbUint(request, 1, 0), &team))
    {
        answerError(response, ERR_NOT_FOUND);
        return;
    }
    pbSetUint(&r, 1, ERR_SUCCESS);
    teamMessage(&team, &msg);
    pbSetMsg(&r, 2, &msg);
    teamMembers((unsigned)pbUint(&team, TEAM_ID, 0), &r, 3, &cars);
    answer(response, &r);
    pbFree(&r);
    pbFree(&msg);
    pbFree(&team);
}

static void loadTeamApplicants(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg team, r = {0};
    pbSetUint(&r, 1, ERR_SUCCESS);
    if (loadTeam((unsigned)pbUint(request, 1, 0), &team))
    {
        teamApplicants(&team, &r, 2);
        pbFree(&team);
    }
    answer(response, &r);
    pbFree(&r);
}

static void updateTeam(const PbMsg *request, Wmmt4Response *response)
{
    unsigned userId = (unsigned)pbUint(request, 1, 0);
    PbMsg team = {0};
    if (!loadTeam(userTeam(userId), &team) || pbUint(&team, TEAM_LEADER, 0) != userId)
    {
        pbFree(&team);
        answerError(response, ERR_FORBIDDEN);
        return;
    }
    if (pbHas(request, 2))
        pbSetUint(&team, TEAM_STICKER_FONT, pbUint(request, 2, 0));
    if (pbHas(request, 3))
        pbSetUint(&team, TEAM_CLOSED, pbUint(request, 3, 0));
    saveTeam(&team);
    pbFree(&team);
    answerError(response, ERR_SUCCESS);
}

// The leadership handed to another member.
static void grantCarRight(const PbMsg *request, Wmmt4Response *response)
{
    unsigned userId = (unsigned)pbUint(request, 1, 0), target = (unsigned)pbUint(request, 2, 0);
    unsigned id = userTeam(userId);
    PbMsg team = {0};
    if (!loadTeam(id, &team) || pbUint(&team, TEAM_LEADER, 0) != userId || userTeam(target) != id)
    {
        pbFree(&team);
        answerError(response, ERR_FORBIDDEN);
        return;
    }
    pbSetUint(&team, TEAM_LEADER, target);
    saveTeam(&team);
    pbFree(&team);
    answerError(response, ERR_SUCCESS);
}

// No team campaign is held.
static void getTeamCampaignStatus(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg r = {0};
    (void)request;
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 2, 0); // applied
    answer(response, &r);
    pbFree(&r);
}

static void preApplyTeamCampaign(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg r = {0};
    (void)request;
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 2, 0); // allowed
    pbSetUint(&r, 3, 0);
    answer(response, &r);
    pbFree(&r);
}

static void applyTeamCampaign(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg r = {0};
    (void)request;
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 2, 0);
    answer(response, &r);
    pbFree(&r);
}

// ---------------------------------------------------------------------------
// Ghost battles, competitions, car campaigns: no other cabinet's ghosts, no
// competition or campaign held.

static void loadGhostBattleInfo(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg r = {0};
    (void)request;
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 4, 0); // has_revenges
    pbSetUint(&r, 5, 0); // has_history
    answer(response, &r);
    pbFree(&r);
}

static void loadGhostBattleHistory(const PbMsg *request, Wmmt4Response *response)
{
    (void)request;
    answerError(response, ERR_SUCCESS);
}

static void loadGhostCompetitionInfo(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg r = {0};
    (void)request;
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 3, 1); // closed
    answer(response, &r);
    pbFree(&r);
}

static void loadGhostCompetitionRanking(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg r = {0};
    (void)request;
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 3, 0); // num_of_participants
    answer(response, &r);
    pbFree(&r);
}

static void loadCarCampaignInfo(const PbMsg *request, Wmmt4Response *response)
{
    PbMsg r = {0};
    (void)request;
    pbSetUint(&r, 1, ERR_SUCCESS);
    pbSetUint(&r, 2, 1); // CAR_CAMPAIGN_NOT_ACCEPTED
    pbSetUint(&r, 3, 0);
    pbSetUint(&r, 4, 0);
    pbSetUint(&r, 5, 0);
    answer(response, &r);
    pbFree(&r);
}

// ---------------------------------------------------------------------------

typedef void (*Rpc)(const PbMsg *, Wmmt4Response *);

static const struct
{
    const char *name;
    Rpc handler;
} rpcs[] = {
    {"register_system_info", registerSystemInfo},
    {"ping", ping},
    {"load_user", loadUserRpc},
    {"create_user", createUserRpc},
    {"ask_access_code", askAccessCode},
    {"create_car", createCar},
    {"load_car", loadCarRpc},
    {"update_car", updateCar},
    {"save_game_result", saveGameResult},
    {"load_time_attack_record", loadTimeAttackRecord},
    {"load_game_history", loadGameHistory},
    {"prepare_car_succession", prepareCarSuccession},
    {"succeed_car", succeedCar},
    {"save_terminal_result", saveTerminalResult},
    {"check_team_name", checkTeamName},
    {"create_team", createTeam},
    {"search_team", searchTeam},
    {"load_team_information", loadTeamInformation},
    {"join_team", joinTeam},
    {"approve_team_applicants", approveTeamApplicants},
    {"leave_team", leaveTeam},
    {"dissolve_team", dissolveTeam},
    {"load_team_members", loadTeamMembers},
    {"load_team_applicants", loadTeamApplicants},
    {"update_team", updateTeam},
    {"grant_car_right", grantCarRight},
    {"get_team_campaign_status", getTeamCampaignStatus},
    {"pre_apply_team_campaign", preApplyTeamCampaign},
    {"apply_team_campaign", applyTeamCampaign},
    {"load_ghost_battle_info", loadGhostBattleInfo},
    {"load_ghost_battle_history", loadGhostBattleHistory},
    {"load_ghost_competition_info", loadGhostCompetitionInfo},
    {"load_ghost_competition_ranking", loadGhostCompetitionRanking},
    {"load_car_campaign_info", loadCarCampaignInfo},
};

// Answered with success alone: what the game only reports.
static const char *const reports[] = {"update_user_lock",
                                      "register_system_stats",
                                      "register_ghost_trail",
                                      "save_charge",
                                      "save_multi_game_result",
                                      "update_team_stickers",
                                      "register_opponent_ghost",
                                      "lock_crown",
                                      "save_face_recognition_result",
                                      "save_car_campaign_info",
                                      "accept_car_campaign",
                                      "load_revenges",
                                      NULL};

void wmmt4RpcHandle(const char *path, const uint8_t *body, size_t length, Wmmt4Response *response)
{
    char name[64];
    PbMsg request = {0};

    pthread_mutex_lock(&lock);
    if (sscanf(path, "/method/%63[a-z_0-9]", name) == 1)
    {
        if (wmmt4ServerTrace)
        {
            // The last request of each method, for protoc --decode_raw
            char file[128];
            mkdir(DIRECTORY "/trace", 0755);
            snprintf(file, sizeof(file), DIRECTORY "/trace/%s.pb", name);
            FILE *f = fopen(file, "wb");
            if (f)
            {
                fwrite(body, 1, length, f);
                fclose(f);
            }
        }
        if (!pbParse(&request, body, length))
            pbFree(&request);
        for (size_t i = 0; i < sizeof(rpcs) / sizeof(rpcs[0]); i++)
            if (!strcmp(name, rpcs[i].name))
            {
                rpcs[i].handler(&request, response);
                goto done;
            }
        for (const char *const *r = reports; *r; r++)
            if (!strcmp(name, *r))
            {
                answerError(response, ERR_SUCCESS);
                goto done;
            }
    }
    else if (sscanf(path, "/resource/%63[a-z_0-9]", name) == 1)
    {
        if (!strcmp(name, "ranking"))
            ranking(response);
        else if (!strcmp(name, "place_list"))
            placeList(response);
        else if (!strcmp(name, "car_summary"))
            carSummary(response);
        else if (!strcmp(name, "ghost_summary") || !strcmp(name, "ghost_list"))
            ghosts(strchr(path, '?'), response);
        else if (!strcmp(name, "crown_list") || !strcmp(name, "active_team_list") || !strcmp(name, "file_list") ||
                 !strcmp(name, "car_summary_count"))
        {
            PbMsg empty = {0};
            answer(response, &empty); // nothing listed
        }
    }
    if (response->status != 200)
        fprintf(stderr, "WMMT4 server: %s is not answered\n", path);
done:
    pbFree(&request);
    pthread_mutex_unlock(&lock);
}
