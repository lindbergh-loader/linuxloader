#pragma once

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

int sharedKswap_collect(int pages);
char *sharedStrncpy(char *dest, const char *src, size_t n);
int sharedSetenv(const char *name, const char *value, int overwrite);
char *sharedGetenv(const char *name);
int sharedUnsetenv(const char *name);
char *shared__strdup(const char *string);
struct tm *sharedLocaltime_R(const time_t *timep, struct tm *result);
int sharedIopl(int level);

void *sharedDlopen(const char *filename, int flags);
void *sharedDlsym(void *handle, const char *symbol);
int sharedDlclose(void *handle);
char *sharedDlerror();

#ifdef __cplusplus
}
#endif