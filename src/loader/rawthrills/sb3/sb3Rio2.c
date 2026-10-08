// Superbikes 3's RIO2 library (libRIO2.so), as the game's Rio2 class calls
// it. The game opens the RIO2 first and falls back to the RIO1 when none
// answers (GameInputManager): this one never connects, so the game drives
// the RIO1 that linuxloader64.so stands in for (rawthrills/nerf/nerfRio.c).
#include <stddef.h>
#include <stdint.h>

enum
{
    RIO2_OK = 0,
    RIO2_NOT_CONNECTED = 3,
};

int rio2_init(void) { return RIO2_OK; }
int rio2_shutdown(void) { return RIO2_OK; }
unsigned rio2_enumerate(void) { return 0; }
const char *rio2_serialno(unsigned index) { (void)index; return NULL; }

int rio2_dev_init(int *hrio2, const char *serialno)
{
    (void)serialno;
    *hrio2 = 1;
    return RIO2_OK;
}

int rio2_dev_release(int h) { (void)h; return RIO2_OK; }

int rio2_get_lib_info(void *info) { (void)info; return RIO2_NOT_CONNECTED; }
int rio2_connected(int h) { (void)h; return RIO2_NOT_CONNECTED; }
int rio2_update(int h) { (void)h; return RIO2_NOT_CONNECTED; }
int rio2_flush(int h) { (void)h; return RIO2_NOT_CONNECTED; }
int rio2_info(int h, void *info) { (void)h; (void)info; return RIO2_NOT_CONNECTED; }
int rio2_throw_bone(int h, uint16_t sec) { (void)h; (void)sec; return RIO2_NOT_CONNECTED; }
int rio2_watchdog_throw_bone(int h, uint16_t sec) { (void)h; (void)sec; return RIO2_NOT_CONNECTED; }
int rio2_watchdog_disable(int h) { (void)h; return RIO2_NOT_CONNECTED; }
int rio2_watchdog_clear_count(int h) { (void)h; return RIO2_NOT_CONNECTED; }
int rio2_comm_secure_init(int h, void *pub, void *priv) { (void)h; (void)pub; (void)priv; return RIO2_NOT_CONNECTED; }
int rio2_comm_secure_check(int h, void *host, void *rio2) { (void)h; (void)host; (void)rio2; return RIO2_NOT_CONNECTED; }

int rio2_pin_get_value(int h, int pin, uint32_t *value)
{
    (void)h; (void)pin;
    *value = 0;
    return RIO2_NOT_CONNECTED;
}
int rio2_pin_set_value(int h, int pin, uint32_t value) { (void)h; (void)pin; (void)value; return RIO2_NOT_CONNECTED; }
int rio2_pinmode_config_begin(int h) { (void)h; return RIO2_NOT_CONNECTED; }
int rio2_pinmode_config_end(int h) { (void)h; return RIO2_NOT_CONNECTED; }
int rio2_pinmode_config(int h, int pin, int mode) { (void)h; (void)pin; (void)mode; return RIO2_NOT_CONNECTED; }
int rio2_pinmode_preset(int h, int pin, int preset) { (void)h; (void)pin; (void)preset; return RIO2_NOT_CONNECTED; }
int rio2_pinmode_inp_digital_config(int h, int pin, int polarity, uint8_t toActive, uint8_t toInactive)
{
    (void)h; (void)pin; (void)polarity; (void)toActive; (void)toInactive;
    return RIO2_NOT_CONNECTED;
}
int rio2_pinmode_outp_config(int h, int pin, uint32_t value) { (void)h; (void)pin; (void)value; return RIO2_NOT_CONNECTED; }
int rio2_pinmode_query(int h, int pin, int timestate, int *mode, void *extras)
{
    (void)h; (void)pin; (void)timestate; (void)extras;
    *mode = 0;
    return RIO2_NOT_CONNECTED;
}
int rio2_pinmode_available(int h, int pin, int mode) { (void)h; (void)pin; (void)mode; return RIO2_NOT_CONNECTED; }
int rio2_audio_cfg_set(int h, uint32_t cfg, uint8_t asDefault) { (void)h; (void)cfg; (void)asDefault; return RIO2_NOT_CONNECTED; }
int rio2_audio_cfg_get(int h, void *cfg) { (void)h; (void)cfg; return RIO2_NOT_CONNECTED; }
int rio2_switch_matrix_init(int h, uint8_t banks, uint8_t perBank, const int *outp, const int *inp)
{
    (void)h; (void)banks; (void)perBank; (void)outp; (void)inp;
    return RIO2_NOT_CONNECTED;
}
int rio2_switch_matrix_value(int h, uint8_t bank, uint8_t sw, uint32_t *value)
{
    (void)h; (void)bank; (void)sw;
    *value = 0;
    return RIO2_NOT_CONNECTED;
}
int rio2_ticket_mech_init(int h, uint8_t idx, int notch, int drive) { (void)h; (void)idx; (void)notch; (void)drive; return RIO2_NOT_CONNECTED; }
int rio2_ticket_mech_cfg_get(int h, uint8_t idx, void *cfg) { (void)h; (void)idx; (void)cfg; return RIO2_NOT_CONNECTED; }
int rio2_ticket_mech_cfg_set(int h, uint8_t idx, void *cfg) { (void)h; (void)idx; (void)cfg; return RIO2_NOT_CONNECTED; }
int rio2_ticket_mech_status_get(int h, uint8_t idx, void *status) { (void)h; (void)idx; (void)status; return RIO2_NOT_CONNECTED; }
int rio2_ticket_mech_add(int h, uint8_t idx, uint32_t n) { (void)h; (void)idx; (void)n; return RIO2_NOT_CONNECTED; }
int rio2_ticket_mech_clear(int h, uint8_t idx) { (void)h; (void)idx; return RIO2_NOT_CONNECTED; }
int rio2_waveform_generator_init(int h, uint8_t idx, uint8_t n, const int *pins, const int *modes, const uint16_t *defaults)
{
    (void)h; (void)idx; (void)n; (void)pins; (void)modes; (void)defaults;
    return RIO2_NOT_CONNECTED;
}
int rio2_waveform_load(int h, uint8_t idx, uint8_t n, const void *steps) { (void)h; (void)idx; (void)n; (void)steps; return RIO2_NOT_CONNECTED; }
int rio2_waveform_start(int h, uint8_t idx, int16_t loops, uint8_t reset) { (void)h; (void)idx; (void)loops; (void)reset; return RIO2_NOT_CONNECTED; }
int rio2_waveform_stop(int h, uint8_t idx) { (void)h; (void)idx; return RIO2_NOT_CONNECTED; }
int rio2_coin_meter_init(int h, uint8_t idx, int drive) { (void)h; (void)idx; (void)drive; return RIO2_NOT_CONNECTED; }
int rio2_coin_meter_cfg_get(int h, uint8_t idx, void *cfg) { (void)h; (void)idx; (void)cfg; return RIO2_NOT_CONNECTED; }
int rio2_coin_meter_cfg_set(int h, uint8_t idx, void *cfg) { (void)h; (void)idx; (void)cfg; return RIO2_NOT_CONNECTED; }
int rio2_coin_meter_status_get(int h, uint8_t idx, void *status) { (void)h; (void)idx; (void)status; return RIO2_NOT_CONNECTED; }
int rio2_coin_meter_add(int h, uint8_t idx, uint32_t n) { (void)h; (void)idx; (void)n; return RIO2_NOT_CONNECTED; }
