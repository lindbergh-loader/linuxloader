// libarcaderegistry.so.1 for Namco ES1 games (Gundam: Senjo no Kizuna): the
// cabinet's key/value store (on its /dev/crypt/registry volume, read with
// arcaderegctl by the boot scripts), kept here in a text file in the game's
// directory, arcade-registry.ini ("KEY=value" lines), editable by hand.
//
// The games use six calls: arcade_registry_new() returns a handle (NULL:
// failure), _load(handle) fills it (negative: failure), _read(handle, key)
// returns the value (NULL: not set; the caller copies it), _write(handle,
// key, value), _commit(handle) stores it, _free(handle).
//
// GKE_SEAL_DEBUG_NOCHECK_PROJECTOR is set unless the file has it: the
// cabinet's projector check is skipped (the boot scripts' own debug switch),
// as there is no projector on its serial port. Its value names the
// projector the game would have found (T250: 1024x768).

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define REGISTRY_FILE "arcade-registry.ini"
#define MAX_ENTRIES 256
#define MAX_KEY 128
#define MAX_VALUE 512

typedef struct
{
    char key[MAX_KEY];
    char value[MAX_VALUE];
} Entry;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Entry entries[MAX_ENTRIES];
static int entryCount, loaded;

static Entry *find(const char *key)
{
    for (int i = 0; i < entryCount; i++)
        if (!strcmp(entries[i].key, key))
            return &entries[i];
    return NULL;
}

static void set(const char *key, const char *value)
{
    Entry *e = find(key);
    if (!e)
    {
        if (entryCount >= MAX_ENTRIES)
            return;
        e = &entries[entryCount++];
        snprintf(e->key, sizeof(e->key), "%s", key);
    }
    snprintf(e->value, sizeof(e->value), "%s", value ? value : "");
}

static void loadLocked(void)
{
    char line[MAX_KEY + MAX_VALUE + 4];
    FILE *f;

    if (loaded)
        return;
    loaded = 1;
    if ((f = fopen(REGISTRY_FILE, "r")))
    {
        while (fgets(line, sizeof(line), f))
        {
            char *eq = strchr(line, '=');
            if (line[0] == '#' || !eq)
                continue;
            *eq = '\0';
            eq[strcspn(eq + 1, "\r\n") + 1] = '\0';
            set(line, eq + 1);
        }
        fclose(f);
    }
    if (!find("GKE_SEAL_DEBUG_NOCHECK_PROJECTOR"))
        set("GKE_SEAL_DEBUG_NOCHECK_PROJECTOR", "T250");
}

static void saveLocked(void)
{
    FILE *f = fopen(REGISTRY_FILE, "w");
    if (!f)
        return;
    fprintf(f, "# The cabinet's registry (libarcaderegistry), KEY=value.\n");
    for (int i = 0; i < entryCount; i++)
        fprintf(f, "%s=%s\n", entries[i].key, entries[i].value);
    fclose(f);
}

// The handle is opaque to the games.
static int handleTag;

void *arcade_registry_new(void)
{
    return &handleTag;
}

int arcade_registry_load(void *handle)
{
    if (handle != &handleTag)
        return -1;
    pthread_mutex_lock(&lock);
    loadLocked();
    pthread_mutex_unlock(&lock);
    return 0;
}

const char *arcade_registry_read(void *handle, const char *key)
{
    const char *value = NULL;
    if (handle != &handleTag || !key)
        return NULL;
    pthread_mutex_lock(&lock);
    loadLocked();
    Entry *e = find(key);
    if (e)
        value = e->value;
    pthread_mutex_unlock(&lock);
    return value;
}

int arcade_registry_write(void *handle, const char *key, const char *value)
{
    if (handle != &handleTag || !key)
        return -1;
    pthread_mutex_lock(&lock);
    loadLocked();
    set(key, value);
    pthread_mutex_unlock(&lock);
    return 0;
}

int arcade_registry_commit(void *handle)
{
    if (handle != &handleTag)
        return -1;
    pthread_mutex_lock(&lock);
    saveLocked();
    pthread_mutex_unlock(&lock);
    return 0;
}

void arcade_registry_free(void *handle)
{
    (void)handle;
}
