/******************************************************************************
 * Copyright (C) 2014 -2016  Espressif System
 *
 * FileName: user_plugs.c
 *
 * Description: plug demo's function realization
 *
 * Modification history:
 * 2015/7/1, v1.0 create this file.
*******************************************************************************/
#include "esp_common.h"
#include "user_config.h"
#if FX2N_DEVICE
#include "user_fx2n.h"

#define __countof(a) (sizeof(a) / sizeof(a[0]))
#define __perSize(a) (sizeof(a[0]))

LOCAL struct fx2n_saved_param fx2n_param;
LOCAL struct keys_param keys;
LOCAL struct single_key_param *single_key[FX2N_KEY_NUM];
LOCAL os_timer_t link_led_timer;
LOCAL uint8 link_led_level = 0;

LOCAL void user_fx2n_read_param(void)
{
    spi_flash_read((PRIV_PARAM_START_SEC + PRIV_PARAM_SAVE) * SPI_FLASH_SEC_SIZE,
                   (uint32 *)&fx2n_param, sizeof(struct fx2n_saved_param));
}

LOCAL void
user_fx2n_save_param(void)
{
    spi_flash_erase_sector(PRIV_PARAM_START_SEC + PRIV_PARAM_SAVE);
    spi_flash_write((PRIV_PARAM_START_SEC + PRIV_PARAM_SAVE) * SPI_FLASH_SEC_SIZE,
                    (uint32 *)&fx2n_param, sizeof(struct fx2n_saved_param));
}

u32 user_fx2n_reg_bits(u8 addr_type) {
    return fx_reg_bits(addr_type);
}

u8 user_fx2n_set_run(u8 run)
{
    return true;
}
u8 user_fx2n_run_status(void)
{
    return true;
}

#define SWITCH_FLAG 0x0A
LOCAL bool
switch_is_ok(void)
{
    u8 f;
    f = (fx2n_param.serial_switch_state >> 4) & 0xf;
    return (f == SWITCH_FLAG);
}

LOCAL void
switch_set(bool new_value)
{
    u8 f;

    f = SWITCH_FLAG | new_value;
    fx2n_param.serial_switch_state = f;
}

LOCAL bool
switch_get(void)
{
    return fx2n_param.serial_switch_state & 0x1;
}

LOCAL void
set_uart_take_state(void)
{
    if (switch_get()) {
        fx_uart_take();
    } else {
        fx_uart_release();
    }
}

LOCAL void
user_fx2n_serial_switch_init(void)
{
    if (!switch_is_ok()) {
        switch_set(0);
    }
    PIN_FUNC_SELECT(FX2N_SERIAL_SWITCH_IO_MUX, FX2N_SERIAL_SWITCH_IO_FUNC);
    user_fx2n_serial_switch(switch_get());
}

u8 user_fx2n_serial_switch(u8 cmd)
{
    switch_set(!!cmd);
    GPIO_OUTPUT_SET(GPIO_ID_PIN(FX2N_LINK_LED_IO_NUM), switch_get());
    user_fx2n_save_param();
    set_uart_take_state();
    return switch_get();
}

u8 user_fx2n_serial_switch_status(void){
    return fx2n_param.serial_switch_state;
}

/******************************************************************************
 * FunctionName : user_fx2n_short_press
 * Description  : key's short press function, needed to be installed
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
LOCAL void
user_fx2n_short_press(void)
{
    user_fx2n_save_param();
}

/******************************************************************************
 * FunctionName : user_fx2n_long_press
 * Description  : key's long press function, needed to be installed
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
LOCAL void
user_fx2n_long_press(void)
{
    int boot_flag = 12345;
    user_esp_platform_set_active(0);
    system_restore();
    system_rtc_mem_write(70, &boot_flag, sizeof(boot_flag));
    printf("long_press boot_flag %d  \n", boot_flag);
    system_rtc_mem_read(70, &boot_flag, sizeof(boot_flag));
    printf("long_press boot_flag %d  \n", boot_flag);
#if RESTORE_KEEP_TIMER
    user_platform_timer_bkup();
#endif
    system_restart();
}

/******************************************************************************
 * FunctionName : user_link_led_init
 * Description  : int led gpio
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
LOCAL void
user_link_led_init(void)
{
    PIN_FUNC_SELECT(FX2N_LINK_LED_IO_MUX, FX2N_LINK_LED_IO_FUNC);
}

LOCAL void
user_link_led_timer_cb(void)
{
    link_led_level = (~link_led_level) & 0x01;
    GPIO_OUTPUT_SET(GPIO_ID_PIN(FX2N_LINK_LED_IO_NUM), link_led_level);
}

void
user_link_led_timer_init(int time)
{
    os_timer_disarm(&link_led_timer);
    os_timer_setfn(&link_led_timer, (os_timer_func_t *)user_link_led_timer_cb, NULL);
    os_timer_arm(&link_led_timer, time, 1);
    link_led_level = 0;
    GPIO_OUTPUT_SET(GPIO_ID_PIN(FX2N_LINK_LED_IO_NUM), link_led_level);
}
/*
void
user_link_led_timer_done(void)
{
    os_timer_disarm(&link_led_timer);

    GPIO_OUTPUT_SET(GPIO_ID_PIN(FX2N_LINK_LED_IO_NUM), 1);
}
*/
/******************************************************************************
 * FunctionName : user_link_led_output
 * Description  : led flash mode
 * Parameters   : mode, on/off/xhz
 * Returns      : none
*******************************************************************************/
void
user_link_led_output(uint8 mode)
{
    switch (mode)
    {
    case LED_OFF:
        os_timer_disarm(&link_led_timer);
        GPIO_OUTPUT_SET(GPIO_ID_PIN(FX2N_LINK_LED_IO_NUM), 1);
        break;
    case LED_ON:
        os_timer_disarm(&link_led_timer);
        GPIO_OUTPUT_SET(GPIO_ID_PIN(FX2N_LINK_LED_IO_NUM), 0);
        break;
    case LED_1HZ:
        user_link_led_timer_init(1000);
        break;
    case LED_5HZ:
        user_link_led_timer_init(200);
        break;
    case LED_20HZ:
        user_link_led_timer_init(50);
        break;
    default:
        printf("ERROR:LED MODE WRONG!\n");
        break;
    }
}

/******************************************************************************
 * FunctionName : user_get_key_status
 * Description  : a
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
BOOL
user_get_key_status(void)
{
    return get_key_status(single_key[0]);
}

LOCAL void
user_fx2n_key_init(void)
{
    single_key[0] = key_init_single(FX2N_KEY_0_IO_NUM, FX2N_KEY_0_IO_MUX, FX2N_KEY_0_IO_FUNC,
                                    user_fx2n_long_press, user_fx2n_short_press);
    keys.key_num = FX2N_KEY_NUM;
    keys.single_key = single_key;
    key_init(&keys);
}

/******************************************************************************
 * FunctionName : user_fx2n_init
 * Description  : init plug's key function and relay output
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void
user_fx2n_init(void)
{
    printf("user_fx2n_init.\n");
    fx_init();
    user_link_led_init();
    wifi_status_led_install(FX2N_WIFI_LED_IO_NUM, FX2N_WIFI_LED_IO_MUX, FX2N_WIFI_LED_IO_FUNC);
    user_fx2n_key_init();
    user_fx2n_read_param();
    user_fx2n_serial_switch_init();
}
#endif
