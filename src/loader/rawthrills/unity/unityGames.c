// The Raw Thrills Unity games, each its own module (see unity.h): the game
// whose data directory the executable has, else the one with none (Nerf
// Arcade, the first of them).
#include "unity.h"

extern const UnityGame nerfGame;

const UnityGame *const unityGames[] = {
    &nerfGame,
    NULL,
};
