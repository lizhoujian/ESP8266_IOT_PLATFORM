#ifndef __USER_FX2N_H__
#define __USER_FX2N_H__
#include "user_fx.h"
#include "driver/key.h"

/* NOTICE---this is for 512KB spi flash.
 * you can change to other sector if you use other size spi flash. */
#define PRIV_PARAM_START_SEC        0x7C

#define PRIV_PARAM_SAVE     0

#define PLUG_KEY_NUM            1

#define PLUG_KEY_0_IO_MUX     PERIPHS_IO_MUX_MTCK_U
#define PLUG_KEY_0_IO_NUM     13
#define PLUG_KEY_0_IO_FUNC    FUNC_GPIO13

#define PLUG_WIFI_LED_IO_MUX     PERIPHS_IO_MUX_GPIO0_U
#define PLUG_WIFI_LED_IO_NUM     0
#define PLUG_WIFI_LED_IO_FUNC    FUNC_GPIO0

#define PLUG_LINK_LED_IO_MUX     PERIPHS_IO_MUX_MTDI_U
#define PLUG_LINK_LED_IO_NUM     12
#define PLUG_LINK_LED_IO_FUNC    FUNC_GPIO12

#define PLUG_RELAY_LED_IO_MUX     PERIPHS_IO_MUX_MTDO_U
#define PLUG_RELAY_LED_IO_NUM     15
#define PLUG_RELAY_LED_IO_FUNC    FUNC_GPIO15

#define PLUG_STATUS_OUTPUT(pin, on)     GPIO_OUTPUT_SET(pin, on)

enum {
    LED_OFF = 0,
    LED_ON  = 1,
    LED_1HZ,
    LED_5HZ,
    LED_20HZ,
};

#define MAX_SWITCH_SOCKET 32
struct fx2n_saved_param {
    uint8_t status[MAX_SWITCH_SOCKET];
    uint32_t pad[1];
};

void user_fx2n_init(void);
uint8_t user_fx2n_count(void);

char *user_fx2n_get_status_string(char *s, int len);
void user_fx2n_set_status_string(char *s, int len);

/* old style */
uint32_t user_fx2n_get_status_int(void);
void user_fx2n_set_status_int(uint32_t num, uint32_t v);

/* new style */
bool user_fx2n_get_status(uint8_t index);
void user_fx2n_set_status(uint8_t index, bool status);

BOOL user_get_key_status(void);

#endif
