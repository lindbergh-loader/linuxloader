// Teenage Mutant Ninja Turtles (Raw Thrills, TMNT_Data): a 64-bit Unity
// game on the Raw Thrills Unity base (rawthrills/unity), up to five players
// on a RIO2: a virtual board here, its GPIO and switch matrix fed by the
// players' inputs. Its natives: tmntNatives.c, its video decoder:
// tmntPlugins.c.
#include "../unity/unity.h"

// TMNT's GameInput values, read in groups of one GPIO bank and seven
// switch-matrix rows for each player. These names also let EVDEV configs
// address the same controls as the other Raw Thrills games.
static const UnitySwitchKey tmntSwitches[] = {
    {0, "TEST_BUTTON"}, {1, "PLAYER_1_BUTTON_SERVICE"}, {2, "VOLUME_UP"},
    {3, "VOLUME_DOWN"}, {4, "PLAYER_1_COIN"}, {5, "PLAYER_2_COIN"},
    {6, "PLAYER_3_COIN"}, {7, "PLAYER_4_COIN"}, {8, "PLAYER_5_COIN"},
    {11, "PLAYER_1_BUTTON_START"}, {12, "PLAYER_2_BUTTON_START"}, {13, "PLAYER_3_BUTTON_START"},
    {14, "PLAYER_4_BUTTON_START"},
    {15, "PLAYER_1_BUTTON_UP"}, {16, "PLAYER_2_BUTTON_UP"}, {17, "PLAYER_3_BUTTON_UP"}, {18, "PLAYER_4_BUTTON_UP"}, {19, "PLAYER_5_BUTTON_UP"},
    {20, "PLAYER_1_BUTTON_DOWN"}, {21, "PLAYER_2_BUTTON_DOWN"}, {22, "PLAYER_3_BUTTON_DOWN"}, {23, "PLAYER_4_BUTTON_DOWN"}, {24, "PLAYER_5_BUTTON_DOWN"},
    {25, "PLAYER_1_BUTTON_LEFT"}, {26, "PLAYER_2_BUTTON_LEFT"}, {27, "PLAYER_3_BUTTON_LEFT"}, {28, "PLAYER_4_BUTTON_LEFT"}, {29, "PLAYER_5_BUTTON_LEFT"},
    {30, "PLAYER_1_BUTTON_RIGHT"}, {31, "PLAYER_2_BUTTON_RIGHT"}, {32, "PLAYER_3_BUTTON_RIGHT"}, {33, "PLAYER_4_BUTTON_RIGHT"}, {34, "PLAYER_5_BUTTON_RIGHT"},
    {35, "PLAYER_1_BUTTON_1"}, {36, "PLAYER_2_BUTTON_1"}, {37, "PLAYER_3_BUTTON_1"}, {38, "PLAYER_4_BUTTON_1"}, {39, "PLAYER_5_BUTTON_1"},
    {40, "PLAYER_1_BUTTON_2"}, {41, "PLAYER_2_BUTTON_2"}, {42, "PLAYER_3_BUTTON_2"}, {43, "PLAYER_4_BUTTON_2"}, {44, "PLAYER_5_BUTTON_2"},
    {45, "PLAYER_1_BUTTON_3"}, {46, "PLAYER_2_BUTTON_3"}, {47, "PLAYER_3_BUTTON_3"}, {48, "PLAYER_4_BUTTON_3"}, {49, "PLAYER_5_BUTTON_3"},
    {0, NULL},
};

// A four-player keyboard layout for TMNT. WASD/J/K/L is player one;
// arrows/Return/Right Shift/Slash is player two. The next two sets support
// simultaneous local play on the same keyboard.
static const UnitySwitchKeysym tmntKeys[] = {
    {0, 0xffbf}, {1, 0xffbe}, {2, 0xff55}, {3, 0xff56},
    {4, '5'}, {5, '6'}, {6, '7'}, {7, '8'}, {8, '9'},
    {11, '1'}, {12, '2'}, {13, '3'}, {14, '4'},
    {15, 'w'}, {20, 's'}, {25, 'a'}, {30, 'd'}, {35, 'j'}, {40, 'k'}, {45, 'l'},
    {16, 0xff52}, {21, 0xff54}, {26, 0xff51}, {31, 0xff53},
    {36, 0xff0d}, {41, 0xffe2}, {46, '/'},
    {17, 'i'}, {22, 'k'}, {27, 'j'}, {32, 'l'}, {37, 'u'}, {42, 'o'}, {47, 'p'},
    {18, 't'}, {23, 'g'}, {28, 'f'}, {33, 'h'}, {38, 'r'}, {43, 'y'}, {48, 'u'},
    {0, 0},
};

// GPIO 0-3 are test/service/directions; GPIO 4-8 are coin inputs 1-5.
static int tmntPin(int pin, uint32_t *value)
{
    UnityInput in;
    if (pin < 0 || pin > 8)
        return 0;
    unityInputSample(&in);
    *value = in.switches[pin] ? 1u : 0u;
    return 1;
}

static int tmntMatrix(int bank, int sw, uint32_t *value)
{
    // The first switch-matrix row starts at GameInput index 15 (P1 up);
    // each following row advances five indices, one per player. Starting
    // at 16 drops P1's button 1/2/3 and shifts the later players left.
    int inputIndex = sw < 7 ? 15 + sw * 5 + bank : 11 + bank;
    UnityInput in;
    if (bank >= 4 || sw >= 8)
        return 0;
    unityInputSample(&in);
    *value = in.switches[inputIndex] ? 1u : 0u;
    return 1;
}

const UnityGame tmntGame = {
    .name = "Teenage Mutant Ninja Turtles",
    .dataDir = "/TMNT_Data",
    .switches = tmntSwitches,
    .keys = tmntKeys,
    .rio2Virtual = 1,
    .rio2Pin = tmntPin,
    .rio2Matrix = tmntMatrix,
};
