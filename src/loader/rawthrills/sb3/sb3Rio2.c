// Superbikes 3's disconnected RIO2 stand-in and TMNT's virtual RIO2. TMNT
// requires a connected board, so its switch matrix is fed by nerfInput's
// desktop keys or [EVDEV] bindings.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../nerf/nerf.h"

enum
{
    RIO2_OK = 0,
    RIO2_NOT_CONNECTED = 3,
};

// SB3 deliberately selects its RIO1 fallback. For TMNT every API reports a
// virtual board present; zero remains the RIO2 success code.
#define RIO2_ERROR (nerfGame == NERF_GAME_TMNT ? RIO2_OK : RIO2_NOT_CONNECTED)

int rio2_init(void)
{
    if (nerfGame == NERF_GAME_TMNT)
        nerfInputInit();
    return RIO2_OK;
}
int rio2_shutdown(void) { return RIO2_OK; }
unsigned rio2_enumerate(void) { return nerfGame == NERF_GAME_TMNT ? 1 : 0; }
const char *rio2_serialno(unsigned index) { return nerfGame == NERF_GAME_TMNT && index == 0 ? "LINUXLOADER-TMNT" : NULL; }

int rio2_dev_init(int *hrio2, const char *serialno)
{
    (void)serialno;
    *hrio2 = 1;
    return RIO2_OK;
}

int rio2_dev_release(int h) { (void)h; return RIO2_OK; }

int rio2_get_lib_info(void *info) { (void)info; return RIO2_ERROR; }
int rio2_connected(int h) { (void)h; return RIO2_ERROR; }
int rio2_update(int h) { (void)h; return RIO2_ERROR; }
int rio2_flush(int h) { (void)h; return RIO2_ERROR; }
int rio2_info(int h, void *info) { (void)h; (void)info; return RIO2_ERROR; }
int rio2_throw_bone(int h, uint16_t sec) { (void)h; (void)sec; return RIO2_ERROR; }
int rio2_watchdog_throw_bone(int h, uint16_t sec) { (void)h; (void)sec; return RIO2_ERROR; }
int rio2_watchdog_disable(int h) { (void)h; return RIO2_ERROR; }
int rio2_watchdog_clear_count(int h) { (void)h; return RIO2_ERROR; }
int rio2_comm_secure_init(int h, void *pub, void *priv) { (void)h; (void)pub; (void)priv; return RIO2_ERROR; }
int rio2_comm_secure_check(int h, void *host, void *rio2) { (void)h; (void)host; (void)rio2; return RIO2_ERROR; }

int rio2_pin_get_value(int h, int pin, uint32_t *value)
{
    (void)h;
    *value = 0;
    if (nerfGame == NERF_GAME_TMNT && pin >= 0 && pin <= 8)
    {
        // GPIO 0-3 are test/service/directions; GPIO 4-8 are coin inputs 1-5.
        static const int inputs[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        NerfInput in;
        nerfInputSample(&in);
        *value = in.switches[inputs[pin]] ? 1u : 0u;
        return RIO2_OK;
    }
    return RIO2_ERROR;
}
int rio2_pin_set_value(int h, int pin, uint32_t value) { (void)h; (void)pin; (void)value; return RIO2_ERROR; }
int rio2_pinmode_config_begin(int h) { (void)h; return RIO2_ERROR; }
int rio2_pinmode_config_end(int h) { (void)h; return RIO2_ERROR; }
int rio2_pinmode_config(int h, int pin, int mode) { (void)h; (void)pin; (void)mode; return RIO2_ERROR; }
int rio2_pinmode_preset(int h, int pin, int preset) { (void)h; (void)pin; (void)preset; return RIO2_ERROR; }
int rio2_pinmode_inp_digital_config(int h, int pin, int polarity, uint8_t toActive, uint8_t toInactive)
{
    (void)h; (void)pin; (void)polarity; (void)toActive; (void)toInactive;
    return RIO2_ERROR;
}
int rio2_pinmode_outp_config(int h, int pin, uint32_t value) { (void)h; (void)pin; (void)value; return RIO2_ERROR; }
int rio2_pinmode_query(int h, int pin, int timestate, int *mode, void *extras)
{
    (void)h; (void)pin; (void)timestate; (void)extras;
    *mode = 0;
    return RIO2_ERROR;
}
int rio2_pinmode_available(int h, int pin, int mode) { (void)h; (void)pin; (void)mode; return RIO2_ERROR; }
int rio2_audio_cfg_set(int h, uint32_t cfg, uint8_t asDefault) { (void)h; (void)cfg; (void)asDefault; return RIO2_ERROR; }
int rio2_audio_cfg_get(int h, void *cfg) { (void)h; (void)cfg; return RIO2_ERROR; }
int rio2_switch_matrix_init(int h, uint8_t banks, uint8_t perBank, const int *outp, const int *inp)
{
    (void)h; (void)banks; (void)perBank; (void)outp; (void)inp;
    return RIO2_ERROR;
}
int rio2_switch_matrix_value(int h, uint8_t bank, uint8_t sw, uint32_t *value)
{
    (void)h;
    *value = 0;
    if (nerfGame == NERF_GAME_TMNT && bank < 4 && sw < 8)
    {
        // The first switch-matrix row starts at GameInput index 15 (P1 up);
        // each following row advances five indices, one per player. Starting
        // at 16 drops P1's button 1/2/3 and shifts the later players left.
        int inputIndex = sw < 7 ? 15 + sw * 5 + bank : 11 + bank;
        NerfInput in;
        nerfInputSample(&in);
        *value = in.switches[inputIndex] ? 1u : 0u;
        return RIO2_OK;
    }
    return RIO2_ERROR;
}
int rio2_ticket_mech_init(int h, uint8_t idx, int notch, int drive) { (void)h; (void)idx; (void)notch; (void)drive; return RIO2_ERROR; }
int rio2_ticket_mech_cfg_get(int h, uint8_t idx, void *cfg) { (void)h; (void)idx; (void)cfg; return RIO2_ERROR; }
int rio2_ticket_mech_cfg_set(int h, uint8_t idx, void *cfg) { (void)h; (void)idx; (void)cfg; return RIO2_ERROR; }
int rio2_ticket_mech_status_get(int h, uint8_t idx, void *status) { (void)h; (void)idx; (void)status; return RIO2_ERROR; }
int rio2_ticket_mech_add(int h, uint8_t idx, uint32_t n) { (void)h; (void)idx; (void)n; return RIO2_ERROR; }
int rio2_ticket_mech_clear(int h, uint8_t idx) { (void)h; (void)idx; return RIO2_ERROR; }
int rio2_waveform_generator_init(int h, uint8_t idx, uint8_t n, const int *pins, const int *modes, const uint16_t *defaults)
{
    (void)h; (void)idx; (void)n; (void)pins; (void)modes; (void)defaults;
    return RIO2_ERROR;
}
int rio2_waveform_load(int h, uint8_t idx, uint8_t n, const void *steps) { (void)h; (void)idx; (void)n; (void)steps; return RIO2_ERROR; }
int rio2_waveform_start(int h, uint8_t idx, int16_t loops, uint8_t reset) { (void)h; (void)idx; (void)loops; (void)reset; return RIO2_ERROR; }
int rio2_waveform_stop(int h, uint8_t idx) { (void)h; (void)idx; return RIO2_ERROR; }
int rio2_coin_meter_init(int h, uint8_t idx, int drive) { (void)h; (void)idx; (void)drive; return RIO2_ERROR; }
int rio2_coin_meter_cfg_get(int h, uint8_t idx, void *cfg) { (void)h; (void)idx; (void)cfg; return RIO2_ERROR; }
int rio2_coin_meter_cfg_set(int h, uint8_t idx, void *cfg) { (void)h; (void)idx; (void)cfg; return RIO2_ERROR; }
int rio2_coin_meter_status_get(int h, uint8_t idx, void *status) { (void)h; (void)idx; (void)status; return RIO2_ERROR; }
int rio2_coin_meter_add(int h, uint8_t idx, uint32_t n) { (void)h; (void)idx; (void)n; return RIO2_ERROR; }
