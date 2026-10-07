#ifndef GAME_PATH_H
#define GAME_PATH_H

#include <stddef.h>

// The running game's cabinet paths mapped to their host location, by its
// system (Raw Thrills, Namco ES1 and N2): path, or buf
// holding the mapped one.
const char *gameRedirectPath(const char *path, char *buf, size_t size);

#endif // GAME_PATH_H
