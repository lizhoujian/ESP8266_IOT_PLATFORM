#ifndef __USER_FX2N_H__
#define __USER_FX2N_H__
#include "user_fx.h"
#include "driver/key.h"

/* NOTICE---this is for 512KB spi flash.
 * you can change to other sector if you use other size spi flash. */
#define PRIV_PARAM_START_SEC        0x7C

#define PRIV_PARAM_SAVE     0

#define FX2N_KEY_NUM            1

#define FX2N_KEY_0_IO_MUX     PERIPHS_IO_MUX_MTCK_U
#define FX2N_KEY_0_IO_NUM     13
#define FX2N_KEY_0_IO_FUNC    FUNC_GPIO13

#define FX2N_WIFI_LED_IO_MUX     PERIPHS_IO_MUX_GPIO0_U
#define FX2N_WIFI_LED_IO_NUM     0
#define FX2N_WIFI_LED_IO_FUNC    FUNC_GPIO0

#define FX2N_LINK_LED_IO_MUX     PERIPHS_IO_MUX_MTDI_U
#define FX2N_LINK_LED_IO_NUM     12
#define FX2N_LINK_LED_IO_FUNC    FUNC_GPIO12

#define FX2N_SERIAL_SWITCH_IO_MUX     PERIPHS_IO_MUX_GPIO2_U
#define FX2N_SERIAL_SWITCH_IO_NUM     2
#define FX2N_SERIAL_SWITCH_IO_FUNC    FUNC_GPIO2

#define FX2N_STATUS_OUTPUT(pin, on)     GPIO_OUTPUT_SET(pin, on)

enum
{
    LED_OFF = 0,
    LED_ON  = 1,
    LED_1HZ,
    LED_5HZ,
    LED_20HZ,
};

#define MAX_SWITCH_SOCKET 32
struct fx2n_saved_param
{
    uint8_t status[MAX_SWITCH_SOCKET];
    uint8_t serial_switch_state;
    uint32_t pad[1];
};

void user_fx2n_init(void);

u32 user_fx2n_reg_bits(u8 addr_type);

u8 user_fx2n_set_run(u8 run);
u8 user_fx2n_run_status(void);

u8 user_fx2n_serial_switch(u8 cmd);
u8 user_fx2n_serial_switch_status(void);

void user_fx2n_uart_switch_to_fx(void);
void user_fx2n_uart_switch_to_overnet(void);

BOOL user_get_key_status(void);

#endif
