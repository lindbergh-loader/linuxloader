// The g7 games' descriptors (see g7.h).
#include <stdio.h>
#include "g7.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const G7Game games[] = {
};

const G7Game *g7Find(uintptr_t dynamic)
{
    for (size_t i = 0; i < COUNT(games); i++)
        if (games[i].dynamic == dynamic)
            return &games[i];
    return NULL;
}
