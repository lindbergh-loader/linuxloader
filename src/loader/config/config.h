#pragma once

#include <SDL3/SDL_keycode.h>
#include <stdint.h>
#include <sys/types.h>

#define MAX_PATH_LENGTH 1024
#define INPUT_STRING_LENGTH 256

#define JURASSIC_PARK_RT 0xf7503efd                              // Raw Thrills, v1.33
#define CRUISN_BLAST_RT 0xddaf18ba                               // Raw Thrills, v1.25
#define GALAGA_ASSAULT_RT 0x2986f0ca                             // Raw Thrills
#define WALKING_DEAD_RT 0xf6330a20                               // Raw Thrills (patched copy: libcsv path shortened)
#define TERMINATOR_SALVATION_RT 0xa999339e                       // Raw Thrills, v01.25.00
#define BIG_BUCK_WORLD_RT 0x17633eb3                             // Raw Thrills, v1.20
// Raw Thrills, g5 engine.  This is the CRC of the *patched* copy the loader
// runs: the game links libcsv by absolute path, and shortening that DT_NEEDED
// string falls inside the 0x4000 bytes the id is taken over (0x56adc4aa is
// the untouched file).
#define BIG_BUCK_HD_WILD_RT 0xc814b556
#define PACMAN_CHOMP_MANIA_RT 0xf841f9e7                         // Raw Thrills, v1.28C
#define WHEEL_OF_FORTUNE_RT 0x4a739bbd                           // Raw Thrills, g3 engine
#define DEAL_OR_NO_DEAL_RT 0x7d589b9a                             // Raw Thrills, v01.07.06
#define DEAL_OR_NO_DEAL_UK_RT 0x13c57d44                          // Raw Thrills, v01.06.06 UK
#define DEAL_OR_NO_DEAL_DELUXE_RT 0x0bb3d477                      // Raw Thrills, v01.18.00.NJS
// Raw Thrills, g6 engine; like Big Buck HD Wild, the CRC of the patched copy
// (0xd394437b is the untouched file).
#define PINK_PANTHER_RT 0x2a0ca1ef
#define ALIENS_ARMAGEDDON_RT 0x1e59ce21                          // Raw Thrills, g6 engine
// Raw Thrills, g6 engine, an envelope dump; the CRC of the patched copy
// (0x12b2ad06 is the untouched file).
#define ANGRY_BIRDS_RT 0x5cf3dd99
// Raw Thrills, g6 engine; the CRC of the patched copy, for both TeknoParrot's
// game and the untouched game0.
#define MOTOGP_RT 0x470a9e32
// Ghostbusters Arcade, g6 engine: patched copy (undefined symbol values
// cleared and libcsv path shortened).
// The untouched executable's partial CRC is 0x344ca1b3.
#define GHOSTBUSTERS_RT 0xcaeea463
#define SPACE_INVADERS_FRENZY_RT 0xa3cdb3b6
#define WORLDS_LARGEST_PACMAN_RT 0x604f0e91                         // Raw Thrills, v1.71
#define DOODLE_JUMP_RT 0x7ae081d8                                // ICE (Raw Thrills pm platform), v1.28
#define TIPPIN_BLOKS_RT 0x1b27d8e9                               // ICE (PlayMechanix g3), v1.80 (patched copy)
#define TARGET_TERROR_GOLD_RT 0x595365af                         // Raw Thrills, 2004 engine, Gold (patched copy)
#define BIG_BUCK_HUNTER_PRO_RT 0x5251b97d                        // Raw Thrills, v1.25, Rockey envelope removed
#define NIRIN_ES1 0x71e72ff6                                     // Namco ES1, NRN100-4-NA-DAT0-A37
#define DHRIDERS_ES1 0x5d3df1cd                                  // Namco ES1, DH RIDERS (rev 1247)
#define DEADHEAT_ES1 0x7a3a5554                                  // Namco ES1, US DRIVE (rev 6273)
#define WMMT4_ES1 0x5031e0db                                     // Namco ES1, WMN4r (rev 1.10.18)
#define MAXIMUMHEAT3D_ES1 0xf932e88e                             // Namco ES1, US DRIVE (rev 8807)
#define WMMT3_N2 0xec016afd                                      // Namco N2, WM3100-3-NA-DAT0-A70 (export)
#define WMMT3DXPLUS_N2 0x85a0e7ab                                // Namco N2, W3P100-1-NA-DAT0-B02
#define CSNEO_N2 0xfe266a17                                       // Namco N2, csneo2 (Counter Strike NEO, hlds_amd code segment)
#define TANKTANKTANK_ES1 0xd7270b7d                              // Namco ES1, tank_us_100226_rev1.03.14
#define GUNDAM_KIZUNA_ES1 0xb647d490                             // Namco ES1, Senjo no Kizuna REV3.57.07
#define CROSSFIRE_TEAMPLAY 0x0a8b80a6                            // Teamplay, med_pball
#define PUCK_OFF_GVR 0x71da2491                                  // Global VR, v0710240921
#define AMERICAS_ARMY_GVR 0x1af79b17                             // Global VR, 1.0.1.082 (armyops-bin)
#define TUX_RACER_ROXOR 0xa27ceeb7                               // Roxor, Tux Racer 4.1 ARCADE (tuxracer)
#define POLICE_TRAINER_2_TEAMPLAY 0xf1709a8e                     // Teamplay, pt2s_g11
#define AFTER_BURNER_CLIMAX_SBLR 0xa3c246e2                       // DVP-0009
#define AFTER_BURNER_CLIMAX_SBLR_REVA 0xe657b1c0                  // DVP-0009A
#define AFTER_BURNER_CLIMAX_SBLR_REVB 0xdb9d8396                  // DVP-0009B
#define AFTER_BURNER_CLIMAX_SDX_SBMN 0xc042a0a2                   // DVP-0018
#define AFTER_BURNER_CLIMAX_SDX_SBMN_REVA 0xcb768831              // DVP-0018A
#define AFTER_BURNER_CLIMAX_SE_SBLR 0x86df4e64                    // DVP-0031
#define AFTER_BURNER_CLIMAX_SE_SBLR_REVA 0x684352f4               // DVP-0031A
#define GHOST_SQUAD_EVOLUTION_SBNJ 0x455edaec                     // DVP-0029A
#define HARLEY_DAVIDSON_SBRG 0xb1dd1c12                           // DVP-5007
#define HUMMER_SBQN 0xf3778f44                                    // DVP-0057
#define HUMMER_SBQN_REVB 0x4e6d5c64                               // DVP-0057B
#define HUMMER_EXTREME_SBST 0x7129c32b                            // DVP-0079
#define HUMMER_EXTREME_MDX_SBTL 0xfeaf8484                        // DVP-0083
#define INITIALD_4_SBML_REVA 0x361d1cbe                           // DVP-0019A
#define INITIALD_4_SBML_REVB 0x606914be                           // DVP-0019B
#define INITIALD_4_SBML_REVC 0x50f1c269                           // DVP-0019C
#define INITIALD_4_SBML_REVD 0xba380f8a                           // DVP-0019D
#define INITIALD_4_SBML_REVG 0x6cb19701                           // DVP-0019G
#define INITIALD_4_SBML_REVZ 0xeda16488                           // DVP-0019Z
#define INITIALD_4_SBML_SERVERBOX 0x678f4320                      // DVP-0019
#define INITIALD_4_EXP_SBNK_REVB 0xd0c64f3                        // DVP-0030B
#define INITIALD_4_EXP_SBNK_REVC 0x65ea22e3                       // DVP-0030C
#define INITIALD_4_EXP_SBNK_REVD 0x62cc73a9                       // DVP-0030D
#define INITIALD_5_JAP_SBQZ_REVA 0xb3183112                       // DVP-0070A
#define INITIALD_5_JAP_SBQZ_REVC 0xda04e5e0                       // DVP-0070C
#define INITIALD_5_JAP_SBQZ_REVF 0xfc3dc85d                       // DVP-0070F
#define INITIALD_5_JAP_SBQZ_SERVERBOX 0x5fd379cd                  // DVP-0070A
#define INITIALD_5_EXP_SBRY 0x701b88cf                            // DVP-0075
#define INITIALD_5_EXP20_SBTS 0x77c6b58b                          // DVP-0084
#define INITIALD_5_EXP20_SBTS_REVA 0xd4910e75                     // DVP-0084A
#define LETS_GO_JUNGLE_SBLU 0xb1c8c901                            // DVP-0011
#define LETS_GO_JUNGLE_SBLU_REVA 0xc697c4fb                       // DVP-0011A
#define LETS_GO_JUNGLE_SBNR_SPECIAL 0x240beedc                    // DVP-0036A
#define MJ4_SBPN_REVG 0x57813a7                                   // DVP-0049G
#define MJ4_EVO_SBTA 0xb785a0e9                                   // DVP-0081
#define OUTRUN_2_SP_SDX_SBMB 0x92c196d5                           // DVP-0015
#define OUTRUN_2_SP_SDX_SBMB_REVA 0x4debd5f0                      // DVP-0015A
#define OUTRUN_2_SP_SDX_SBMB_REVA_TEST 0x6b2d5c46                 // DVP-0015A
#define OUTRUN_2_SP_SDX_SBMB_REVA_TEST2 0x4ee20716                // DVP-0015A
#define OUTRUN_2_SP_SDX_SBMB_TEST 0xf1c89eae                      // DVP-0015
#define PRIMEVAL_HUNT_SBPP 0x6868215c                             // DVP-0048A
#define QUIZ_AXA_SBMS 0x434dee4                                   // DVP-0025H
#define QUIZ_AXA_SBUR_LIVE 0x7103fafc                             // DVP-0087D
#define RAMBO_SBQL 0x48f49dd                                      // DVP-0069
#define RAMBO_SBSS_CHINA 0xad864fec                               // DVP-0078
#define R_TUNED_SBQW 0xa68d053d                                   // DVP-0060
#define SEGABOOT 0x0027                                           // DVP-????
#define SEGABOOT_2_4 0x38d56318                                   // DVP-????
#define SEGABOOT_2_4_SYM 0xa518b18b                               // DVP-????
#define SEGABOOT_2_6 0x0030                                       // DVP-????
#define SEGA_RACE_TV_SBPF 0xb30ab16a                              // DVP-0044
#define THE_HOUSE_OF_THE_DEAD_4_SBLC_REVA 0x226281ad              // DVP-0003A
#define THE_HOUSE_OF_THE_DEAD_4_SBLC_REVA_TEST 0x1d1160aa         // DVP-0003A
#define THE_HOUSE_OF_THE_DEAD_4_SBLC_REVB 0xad652b5               // DVP-0003B
#define THE_HOUSE_OF_THE_DEAD_4_SBLC_REVB_TEST 0xbb24fef8         // DVP-0003B
#define THE_HOUSE_OF_THE_DEAD_4_SBLC_REVC 0xe213414d              // DVP-0003C
#define THE_HOUSE_OF_THE_DEAD_4_SBLC_REVC_TEST 0x6210582b         // DVP-0003C
#define THE_HOUSE_OF_THE_DEAD_4_SPECIAL_SBLS 0x8a7c4ac7           // DVP-0010
#define THE_HOUSE_OF_THE_DEAD_4_SPECIAL_SBLS_TEST 0xb896a44a      // DVP-0010
#define THE_HOUSE_OF_THE_DEAD_4_SPECIAL_SBLS_REVB 0x7d6ab3e3      // DVP-0010B
#define THE_HOUSE_OF_THE_DEAD_4_SPECIAL_SBLS_REVB_TEST 0xae47a9fc // DVP-0010B
#define THE_HOUSE_OF_THE_DEAD_EX_SBRC 0x20115a92                  // DVP-0063
#define THE_HOUSE_OF_THE_DEAD_EX_SBRC_TEST 0xd58a0053             // DVP-0063
#define TOO_SPICY_SBMV 0x46bb306e                                 // DVP-0027A
#define TOO_SPICY_SBMV_TEST 0x6510b2e6                            // DVP-0027A
#define UNKNOWN 0xFFFFFFFF                                        // ????????
#define VIRTUA_FIGHTER_5_SBLM 0x71722584                          // DVP-0008
#define VIRTUA_FIGHTER_5_SBQU_REVA 0x9745abb6                     // DVP-0008A
#define VIRTUA_FIGHTER_5_SBQU_REVB 0x8953bd52                     // DVP-0008B
#define VIRTUA_FIGHTER_5_SBQU_REVE 0x4c2edbf6                     // DVP-0008E
#define VIRTUA_FIGHTER_5_SBLM_EXPORT 0xec474630                   // DVP-0043
#define VIRTUA_FIGHTER_5_FINAL_SHOWDOWN_SBUV_REVA 0xbae2be62      // DVP-5019A
#define VIRTUA_FIGHTER_5_FINAL_SHOWDOWN_SBXX_REVB 0x7cee1d81      // DVP-5020
#define VIRTUA_FIGHTER_5_FINAL_SHOWDOWN_SBXX_REVB_6000 0x34c0d02  // DVP-5020 ver 6.00 (Weird public version)
#define VIRTUA_FIGHTER_5_SBQU_R 0x79db39d                         // DVP-5004
#define VIRTUA_FIGHTER_5_SBQU_R_REVD 0x443b6d07                   // DVP-5004D
#define VIRTUA_FIGHTER_5_SBQU_R_REVG 0x4702ae73                   // DVP-5004G
#define VIRTUA_TENNIS_3_SBKX 0x7a021b5                                 // DVP-0005
#define VIRTUA_TENNIS_3_SBKX_TEST 0x3bfbd11e                           // DVP-0005
#define VIRTUA_TENNIS_3_SBKX_REVA 0xa9a10e32                           // DVP-0005A
#define VIRTUA_TENNIS_3_SBKX_REVA_TEST 0xdbcf31c1                      // DVP-0005A
#define VIRTUA_TENNIS_3_SBKX_REVB 0x67776c01                           // DVP-0005B
#define VIRTUA_TENNIS_3_SBKX_REVB_TEST 0xc689beeb                      // DVP-0005B
#define VIRTUA_TENNIS_3_SBKX_REVC 0x55345ed0                           // DVP-0005C
#define VIRTUA_TENNIS_3_SBKX_REVC_TEST 0xdd7c4fea                      // DVP-0005C

typedef enum
{
    YELLOW,
    RED,
    BLUE,
    SILVER,
    REDEX
} Colour;

typedef enum
{
    WORKING,
    NOT_WORKING
} GameStatus;

typedef enum
{
    NOREGION = -1,
    JP = 0,
    US = 1,
    EX = 2
} GameRegion;

typedef enum
{
    SHOOTING,
    DRIVING,
    DIGITAL,
    FLYING,
    MAHJONG
} GameType;

typedef enum
{
    GROUP_UNKNOWN,
    GROUP_ABC,
    GROUP_HOD4,
    GROUP_HOD4_TEST,
    GROUP_HOD4_SP,
    GROUP_HOD4_SP_TEST,
    GROUP_HUMMER,
    GROUP_ID4_EXP,
    GROUP_ID4_JAP,
    GROUP_ID5,
    GROUP_ID_SERVERBOX,
    GROUP_LGJ,
    GROUP_OUTRUN,
    GROUP_OUTRUN_TEST,
    GROUP_RAMBO,
    GROUP_VF5,
    GROUP_VT3,
    GROUP_VT3_TEST
} GameGroup;

typedef enum
{
    AUTO_DETECT_GPU,
    NVIDIA_GPU,
    AMD_GPU,
    ATI_GPU,
    INTEL_GPU,
    UNKNOWN_GPU,
    ERROR_GPU
} GpuType;

typedef struct
{
    unsigned int service;
    unsigned int start;
    unsigned int coin;
    unsigned int up;
    unsigned int down;
    unsigned int left;
    unsigned int right;
    unsigned int button1;
    unsigned int button2;
    unsigned int button3;
    unsigned int button4;
    unsigned int button5;
    unsigned int button6;
    unsigned int button7;
    unsigned int button8;
} PlayerKeyMapping;

typedef struct
{
    unsigned int test;
    PlayerKeyMapping player1;
    PlayerKeyMapping player2;
} KeyMapping;

typedef enum
{
    SEGA_TYPE_1,
    SEGA_TYPE_3,
    NAMCO_NA_JV,
    NAMCO_ES1_JAMMA,
    NAMCO_FCA_1,
    NO_JVS_IO
} JVSIOType;

typedef struct
{
    // Test button
    char test[INPUT_STRING_LENGTH];

    // Exit game
    char exit_game[INPUT_STRING_LENGTH];

    // Player 1 controls
    char player1_button_start[INPUT_STRING_LENGTH];
    char player1_button_service[INPUT_STRING_LENGTH];
    char player1_button_up[INPUT_STRING_LENGTH];
    char player1_button_down[INPUT_STRING_LENGTH];
    char player1_button_left[INPUT_STRING_LENGTH];
    char player1_button_right[INPUT_STRING_LENGTH];
    char player1_button_1[INPUT_STRING_LENGTH];
    char player1_button_2[INPUT_STRING_LENGTH];
    char player1_button_3[INPUT_STRING_LENGTH];
    char player1_button_4[INPUT_STRING_LENGTH];
    char player1_button_5[INPUT_STRING_LENGTH];
    char player1_button_6[INPUT_STRING_LENGTH];
    char player1_button_7[INPUT_STRING_LENGTH];
    char player1_button_8[INPUT_STRING_LENGTH];
    char player1_button_9[INPUT_STRING_LENGTH];
    char player1_button_10[INPUT_STRING_LENGTH];
    char player1_coin[INPUT_STRING_LENGTH];

    // Player 2 controls
    char player2_button_start[INPUT_STRING_LENGTH];
    char player2_button_service[INPUT_STRING_LENGTH];
    char player2_button_up[INPUT_STRING_LENGTH];
    char player2_button_down[INPUT_STRING_LENGTH];
    char player2_button_left[INPUT_STRING_LENGTH];
    char player2_button_right[INPUT_STRING_LENGTH];
    char player2_button_1[INPUT_STRING_LENGTH];
    char player2_button_2[INPUT_STRING_LENGTH];
    char player2_button_3[INPUT_STRING_LENGTH];
    char player2_button_4[INPUT_STRING_LENGTH];
    char player2_button_5[INPUT_STRING_LENGTH];
    char player2_button_6[INPUT_STRING_LENGTH];
    char player2_button_7[INPUT_STRING_LENGTH];
    char player2_button_8[INPUT_STRING_LENGTH];
    char player2_button_9[INPUT_STRING_LENGTH];
    char player2_button_10[INPUT_STRING_LENGTH];
    char player2_coin[INPUT_STRING_LENGTH];

    // Analogue inputs
    char analogue_1[INPUT_STRING_LENGTH];
    char analogue_2[INPUT_STRING_LENGTH];
    char analogue_3[INPUT_STRING_LENGTH];
    char analogue_4[INPUT_STRING_LENGTH];
    char analogue_5[INPUT_STRING_LENGTH];
    char analogue_6[INPUT_STRING_LENGTH];
    char analogue_7[INPUT_STRING_LENGTH];
    char analogue_8[INPUT_STRING_LENGTH];

    int analogue_deadzone_start[8];
    int analogue_deadzone_middle[8];
    int analogue_deadzone_end[8];
} ArcadeInputs;

typedef struct
{
    int emulateRideboard;
    int emulateDriveboard;
    int emulateMotionboard;
    int emulateHW210CardReader;
    int emulateIDCardReader;
    int idCardFileAutoload;
    int emulateTouchscreen;
    char cardFile1[MAX_PATH_LENGTH];
    char cardFile2[MAX_PATH_LENGTH];
    char idCardFolder[MAX_PATH_LENGTH];
    int emulateJVS;
    int fullscreen;
    char eepromPath[MAX_PATH_LENGTH];
    char sramPath[MAX_PATH_LENGTH];
    char libCgPath[MAX_PATH_LENGTH];
    char jvsPath[MAX_PATH_LENGTH];
    char serial1Path[MAX_PATH_LENGTH];
    char serial2Path[MAX_PATH_LENGTH];
    int width;
    int height;
    int boostRenderRes;
    Colour lindberghColour;
    GameStatus gameStatus;
    GameType gameType;
    GameGroup gameGroup;
    KeyMapping keymap;
    uint32_t crc32;
    GameRegion region;
    int freeplay;
    int showDebugMessages;
    int useAltJvsPassthrough;
    char *gameID;
    char *gameTitle;
    char *gameShortTitle;
    char *gameDVP;
    Colour gameLindberghColour;
    char *gameReleaseYear;
    char *gameNativeResolutions;
    GpuType GPUVendor;
    char *GPUVendorString;
    JVSIOType jvsIOType;
    int hummerFlickerFix;
    int keepAspectRatio;
    int fpsLimiter;
    float fpsTarget;
    int lgjRenderWithMesa;
    int ramboGunsSwitch;
    int harleyAnalogRemap;
    int id5ChineseLanguage;
    float idSteeringPercentageReduction;
    int phScreenMode;
    int phTestScreenSingle;
    int phUseZink;
    int disableBuiltinFont;
    int disableBuiltinLogos;
    int hideCursor;
    int customCursorEnabled;
    char customCursor[MAX_PATH_LENGTH];
    int customCursorWidth;
    int customCursorHeight;
    char touchCursor[MAX_PATH_LENGTH];
    int touchCursorWidth;
    int touchCursorHeight;
    int mj4EnabledAtT;
    int enableNetworkPatches;
    // WMMT4: the loader's own game server (card, cars, progress) instead of
    // the dump's offline mode (namco/namcoEs1Wmmt4Server.c).
    int wmmt4Server;
    char idIpSeat1[16];
    char idIpSeat2[16];
    char nicName[20];
    char or2IP[16];
    char or2Netmask[16];
    char IpCab1[16];
    char IpCab2[16];
    char IpCab3[16];
    char IpCab4[16];
    char tooSpicyIpCab1[16];
    char tooSpicyIpCab2[16];
    char srtvIP[16];
    float cpuFreqGhz;
    ArcadeInputs arcadeInputs;
    int inputMode; // 0 = both, 1 = SDL, 2 = EVDEV only
    int skipOutrunCabinetCheck;
    float whiteBorderPercentage;
    float blackBorderPercentage;
    int borderEnabled;
    int rotateVertical;
    char bezel[MAX_PATH_LENGTH];
    int bezelEnabled; // -1: auto, when the image exists
    int enableCrosshairs;
    int gsevoCrosshairAlwaysOn;
    int gsevoCrosshairAlwaysOff;
    char p1CrossHairPath[MAX_PATH_LENGTH];
    char p2CrossHairPath[MAX_PATH_LENGTH];
    int customCrossHairWidth;
    int customCrossHairHeight;
    int fpsOverlayEnabled;
    int fpsOverlayPosition;
} EmulatorConfig;

#ifdef __cplusplus
extern "C" {
#endif

int initConfig(const char *configFilePath);
void setDefaultValues(EmulatorConfig *cfg);
EmulatorConfig *getConfig();
char *getGameName();
char *getDvpName();
char *getGameId();
int getGameLindberghColour();
char *getGameReleaseYear();
char *getGameNativeResolutions();
const char *getLindberghColourString(Colour lindberghColour);
const char *getGameRegionString(GameRegion region);
const char *getGpuTypeString(GpuType gpuType);
extern SDL_Keycode getSDLKeycode(const char *input);

#ifdef __cplusplus
}
#endif
