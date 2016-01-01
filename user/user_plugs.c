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
#if PLUGS_DEVICE
#include "user_plugs.h"

#define __countof(a) (sizeof(a) / sizeof(a[0]))
#define __perSize(a) (sizeof(a[0]))

LOCAL struct plugs_saved_param plugs_param;
LOCAL struct keys_param keys;
LOCAL struct single_key_param *single_key[PLUG_KEY_NUM];
LOCAL os_timer_t link_led_timer;
LOCAL uint8 link_led_level = 0;

static
bool get_bit_status(uint8_t index)
{
    if (index < MAX_SWITCH_SOCKET) {
        return plugs_param.status[index] & 0x1;
    } else {
        printf("get_bit_status, invalid index = %d.\n", index);
        return 0;
    }
}

static
void set_bit_status(uint8_t index, bool status)
{
    if (index < MAX_SWITCH_SOCKET) {
        plugs_param.status[index] = status;
    } else {
        printf("set_bit_status, invalid index = %d.\n", index);
    }
}

uint8_t
user_plugs_count(void)
{
    return MAX_SWITCH_SOCKET;
}

char *
user_plugs_get_status_string(char *s, int len)
{
    int i;
    for (i = 0; i < user_plugs_count() && i < len; i++) {
        s[i] = get_bit_status(i) + '0';
    }
    return s;
}

void
user_plugs_set_status_string(char *s, int len)
{
    int i;
    for (i = 0; i < user_plugs_count() && i < len; i++) {
        set_bit_status(i, s[i] != '0' ? true : false);
    }
}

uint32_t user_plugs_get_status_int(void)
{
    int ret, i;

    ret = 0;
    for (i = 31; i >= 0; i--) {
        ret = (ret << 1) | (plugs_param.status[i] & 0x1);
    }
    return ret;
}

void user_plugs_set_status_int(uint32_t num, uint32_t v)
{
    int i;
    for (i = 0; i < num; i++) {
        user_plugs_set_status(i, (v >> i) & 0x1);
    }
}

/******************************************************************************
 * FunctionName : user_plugs_get_status
 * Description  : get plug's status, 0x00 or 0x01
 * Parameters   : none
 * Returns      : uint8 - plug's status
*******************************************************************************/
bool
user_plugs_get_status(uint8_t index)
{
    return get_bit_status(index);
}

/******************************************************************************
 * FunctionName : user_plugs_set_status
 * Description  : set plugs's status, index, status
 * Parameters   : bool - status
 * Returns      : none
*******************************************************************************/
void
user_plugs_set_status(uint8_t index, bool status)
{
    if (status != get_bit_status(index)) {
        printf("change switch %d,%d\n", index, status);
        set_bit_status(index, status);
        PLUG_STATUS_OUTPUT(PLUG_RELAY_LED_IO_NUM, status);
    }
}

/******************************************************************************
 * FunctionName : user_plugs_short_press
 * Description  : key's short press function, needed to be installed
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_plugs_short_press(void)
{
    spi_flash_erase_sector(PRIV_PARAM_START_SEC + PRIV_PARAM_SAVE);
    spi_flash_write((PRIV_PARAM_START_SEC + PRIV_PARAM_SAVE) * SPI_FLASH_SEC_SIZE,
                (uint32 *)&plugs_param, sizeof(struct plugs_saved_param));
}

/******************************************************************************
 * FunctionName : user_plugs_long_press
 * Description  : key's long press function, needed to be installed
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_plugs_long_press(void)
{
    int boot_flag=12345;
    user_esp_platform_set_active(0);
    system_restore();

    system_rtc_mem_write(70, &boot_flag, sizeof(boot_flag));
    printf("long_press boot_flag %d  \n",boot_flag);
    system_rtc_mem_read(70, &boot_flag, sizeof(boot_flag));
    printf("long_press boot_flag %d  \n",boot_flag);

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
    PIN_FUNC_SELECT(PLUG_LINK_LED_IO_MUX, PLUG_LINK_LED_IO_FUNC);
}

LOCAL void  
user_link_led_timer_cb(void)
{
    link_led_level = (~link_led_level) & 0x01;
    GPIO_OUTPUT_SET(GPIO_ID_PIN(PLUG_LINK_LED_IO_NUM), link_led_level);
}

void  
user_link_led_timer_init(int time)
{
    os_timer_disarm(&link_led_timer);
    os_timer_setfn(&link_led_timer, (os_timer_func_t *)user_link_led_timer_cb, NULL);
    os_timer_arm(&link_led_timer, time, 1);
    link_led_level = 0;
    GPIO_OUTPUT_SET(GPIO_ID_PIN(PLUG_LINK_LED_IO_NUM), link_led_level);
}
/*
void  
user_link_led_timer_done(void)
{
    os_timer_disarm(&link_led_timer);

    GPIO_OUTPUT_SET(GPIO_ID_PIN(PLUG_LINK_LED_IO_NUM), 1);
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

    switch (mode) {
        case LED_OFF:
            os_timer_disarm(&link_led_timer);
            GPIO_OUTPUT_SET(GPIO_ID_PIN(PLUG_LINK_LED_IO_NUM), 1);
            break;
    
        case LED_ON:
            os_timer_disarm(&link_led_timer);
            GPIO_OUTPUT_SET(GPIO_ID_PIN(PLUG_LINK_LED_IO_NUM), 0);
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

/******************************************************************************
 * FunctionName : user_plugs_init
 * Description  : init plug's key function and relay output
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void  
user_plugs_init(void)
{
    int i;
    printf("user_plugs_init start!\n");

    user_link_led_init();

    wifi_status_led_install(PLUG_WIFI_LED_IO_NUM, PLUG_WIFI_LED_IO_MUX, PLUG_WIFI_LED_IO_FUNC);

    single_key[0] = key_init_single(PLUG_KEY_0_IO_NUM, PLUG_KEY_0_IO_MUX, PLUG_KEY_0_IO_FUNC,
                                    user_plugs_long_press, user_plugs_short_press);

    keys.key_num = PLUG_KEY_NUM;
    keys.single_key = single_key;

    key_init(&keys);

#if 0
    spi_flash_read((PRIV_PARAM_START_SEC + PRIV_PARAM_SAVE) * SPI_FLASH_SEC_SIZE,
                (uint32 *)&plugs_param, sizeof(struct plugs_saved_param));

    PIN_FUNC_SELECT(PLUG_RELAY_LED_IO_MUX, PLUG_RELAY_LED_IO_FUNC);

    // default to be off, for safety.
    memset(plugs_param.status, 0, sizeof(plugs_param.status));

    //PLUG_STATUS_OUTPUT(PLUG_RELAY_LED_IO_NUM, plugs_param.status);
#else
    // TODO: read from plc
    memset(plugs_param.status, 0, sizeof(plugs_param.status));
    plugs_param.status[0] = '1';
#endif    
}
#endif

