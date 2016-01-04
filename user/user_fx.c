#include "esp_common.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user_config.h"
#include "user_fx.h"
#include "driver/uart.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define WAIT_RECV_TIMEOUT 2000

enum {
    RECV_INIT = 0,
    RECV_DOING,
    RECV_DONE,
    RECV_ERR,
    RECV_OVERFLOW
};

typedef struct uart_event_t
{
    uint8 event;
} uart_event_t;
enum {
    UART_EVENT_DONE = 200
};

typedef struct uart_buf_t
{
    u16 index;
    u16 len;
    u8 *data;
} uart_buf_t;

static xQueueHandle uart_queue_recv = NULL;
static xSemaphoreHandle recv_buf_mutex = NULL;
static u8 uart_recv_state = RECV_INIT;
static uart_buf_t uart_recv_buf = {0,};

static void set_recv_state(u8 s)
{
    uart_recv_state = s;
}

static void free_recv_buff(void)
{
    uart_buf_t *p = &uart_recv_buf;

    xSemaphoreTake(uart_queue_recv, NULL);    
    if (p->data) {
        p->index = 0;
        p->len = 0;
        free(p->data);
        p->data = NULL;
        printf("free last recv buff.\n");
    }
    xSemaphoreGive(uart_queue_recv, NULL);
}

static bool alloc_recv_buff(u16 data_len)
{
    uart_buf_t *p = &uart_recv_buf;

    xSemaphoreTake(uart_queue_recv, NULL);

    p->len = 1/*STX*/ + data_len + 1/*ETX*/ + 2/*CHECKSUM*/;
    p->data = (u8*)zalloc(p->len);
    p->index = 0;

    xSemaphoreGive(uart_queue_recv, NULL);

    return p->data != NULL;
}

static void post_event(u8 e)
{
    uart_event_t e = {UART_EVENT_DONE,};
    xQueueSendFromISR(uart_queue_recv, (void *)&e, NULL);
}

static void parse_buf(uart_buf_t *p, u16 len)
{
    u16 bytes = len;

    if (bytes > 0 && 
        (p->data[0] == NACK || 
            p->data[0] == ACK)) {
        set_recv_state(RECV_DONE);
        post_event(UART_EVENT_DONE);
    } else if (bytes > 3 && (
        p->data[0] == STX && p->data[bytes - 3] == ETX)) {
        set_recv_state(RECV_DONE);
        post_event(UART_EVENT_DONE);
    } else if (bytes >= p->len) {
        set_recv_state(RECV_OVERFLOW);
        post_event(UART_EVENT_DONE);
    }
}

static bool wait_recv_done(u16 miliseconds)
{
    uart_event_t e = {0,};

    if ((miliseconds % (portTICK_RATE_MS) > 0) {
        miliseconds += portTICK_RATE_MS;
    }
    set_recv_state(RECV_DOING);

    xQueueReceive(uart_queue_recv, (void *)&e, (portTickType)(miliseconds / portTICK_RATE_MS));
    if (e.event = UART_EVENT_DONE) {
        return true;
    } else {
        printf("uart event unkown, event = %d\n", e.event);
        return false;
    }
}

static bool is_ack(void)
{
    uart_buf_t *p = &uart_recv_buf;
    u8 ack;

    xSemaphoreTake(uart_queue_recv, NULL);
    ack = p->data[0];
    xSemaphoreGive(uart_queue_recv, NULL);

    return ack == ACK;
}

/* init response before request */
static bool create_response(u16 data_len)
{
    return alloc_recv_buff(data_len);
}

static bool wait_response(u16 miliseconds)
{
    return wait_recv_done(miliseconds);
}
static void copy_response_data(u8 *out, u16 len)
{
    uart_buf_t *p = &uart_recv_buf;

    xSemaphoreTake(uart_queue_recv, NULL);
    memcpy(out, &p->data[1], len);
    xSemaphoreGive(uart_queue_recv, NULL);
}

static void free_response(void)
{
    free_recv_buff();
}

static void insert_char(u8 c)
{
    uart_buf_t *p = &uart_recv_buf;

    xSemaphoreTakeFromISR(uart_queue_recv, NULL);

    if (p->index < p->len) {
        p->data[p->index] = c;
        p->index++;
        parse_buf(p);
    } else {
        printf("insert char overflow/uninit???\n");
    }

    xSemaphoreGiveFromISR(uart_queue_recv, NULL);
}

static void uart_cb(u8 c)
{
    insert_char(c);
}

static void uart_send(u8 *data, u16 len)
{
    int i;
    for (i = 0; i < len; i++) {
        uart_tx_one_char(UART0, data[i]);
    }
}

void fx_init(void)
{
    uart_queue_recv = xQueueCreate(1, sizeof(os_event_t));
    if (uart_queue_recv) {
        recv_buf_mutex = xSemaphoreCreateMutex();
        if (recv_buf_mutex) {
            uart_init_for_fx();
            uart_set_recv_cb(uart_cb);
        } else {
            vQueueDelete(uart_queue_recv);
            printf("create recv_buf_mutex failed.\n");
        }
    } else {
        printf("create uart_queue_recv failed.\n");
    }
}

#define TO_ASCII(half) (((half) < 10) ? ((half) + 0x30) : ((half) + 0x41 - 0xa))
#define SWAP_BYTE(word) ((((word) >> 8) & 0xff) | (((word) << 8) & 0xff00));

static u32 calc_address(register_t *r, u16 offset)
{
    return r->base_addr + offset;
}

static u32 calc_address2(register_t *r, u16 offset)
{
    return r->base_addr * 2 + offset;
}

static u32 calc_address3(register_t *r, u16 offset)
{
    return r->base_addr * 3 + offset;
}

static u32 swap_address(register_t *r, u32 addr)
{
    return addr;
}

static u32 swap_address2(register_t *r, u32 addr)
{
    u16 addr16 = (u16)addr;
    if (r->addr_len == 2) {
        return SWAP_BYTE(addr16);
    } else {
        return addr;
    }
}

typedef struct register_t
{
    u8 type; /* register type */
    u32 byte_base_addr; /* base address for current */
    u32 bit_base_addr; /* base address for current */
    u32 (*byte_addr)(register_t *r, u16 offset); /* calc byte address */
    u32 (*bit_addr)(register_t *r, u16 offset); /* calc bit address */
    u8 addr_len; /* bytes of address */
} register_t;

static register_t registers[] = {
    {REG_D, REG_D_BASE_ADDRESS, REG_D_BIT_BASE_ADDRESS, calc_address2, calc_address2, 2},
    {REG_M, REG_M_BASE_ADDRESS, REG_M_BIT_BASE_ADDRESS, calc_address2, calc_address2, 2},
    {REG_T, REG_T_BASE_ADDRESS, REG_T_BIT_BASE_ADDRESS, calc_address, calc_address, 2},
    {REG_S, REG_S_BASE_ADDRESS, REG_S_BIT_BASE_ADDRESS, calc_address3, calc_address3, 3},
    {REG_C, REG_C_BASE_ADDRESS, REG_C_BIT_BASE_ADDRESS, calc_address2, calc_address2, 2},
    {REG_X, REG_X_BASE_ADDRESS, REG_X_BIT_BASE_ADDRESS, calc_address, calc_address, 2},
    {REG_Y, REG_Y_BASE_ADDRESS, REG_Y_BIT_BASE_ADDRESS, calc_address, calc_address, 2},
};

#define __countof(a) (sizeof(a) / sizeof(a[0]))

static register_t *find_registers(u8 addr_type)
{
    int i, c = __countof(registers);

    for (i = 0; i < c; i++) {
        if (addr_type == registers[i].type)
            return &registers[i];
    }
    printf("not found register type.");
    return NULL;
}

u8 fx_check_sum(u8 *in, u16 inLen)
{
    int i, sum;

    sum = 0;
    for (i = 0; i < inLen, i++) {
        sum += in[i];
    }

    return sum & 0xff;
}

static void to_ascii(u8 in, u8 *out)
{
    out[0] = TO_ASCII((in >> 4) & 0xf);
    out[1] = TO_ASCII(in & 0xf);
}

void hex_to_ascii(u8 *in, u8 *out, u16 inLen)
{
    int i;
    u8 *p = out;

    for (i = 0; i < inLen; i++) {
        to_ascii(in[i], out + i * 2);
    }
}

static u16 create_request(register_t *r, u8 cmd, u16 addr, u8 *data, u16 len, **req)
{
    u8 *buf;
    u16 buf_len;
    u16 addr;
    u8 sum;

    if (cmd == ACTION_FORCE_ON || cmd == ACTION_FORCE_OFF) {
        addr = r->byte_addr(r, addr);
    } else if (cmd == ACTION_READ || cmd == ACTION_WRITE) {
        addr = r->byte_addr(r, addr);
        addr = SWAP_BYTE(addr);
    }

    buf_len = 1 + 1 + 4;
    if (len > 0) {
        buf_len += 2; /* 2 BYTES */
        buf_len += (len * 2);
    }
    buf_len += 1 + 2;

    buf = (u8*)zalloc(buf_len);
    if (buf) {
        buf[0] = STX;
        buf[1] = TO_ASCII(cmd);
        hex_to_ascii((u8*)&addr, buf[2], 2); /* 4 bytes */
        if (len > 0) {
            to_ascii((u8)len, &buf[4]); /* 2 bytes */
            hex_to_ascii(data, &buf[6], len); /* 2 * len bytes */
        }
        buf[buf_len - 1 - 2] = ETX;        
        sum = fx_check_sum(&buf[1], buf_len - 3 /* - STX - CHECKSUM */);
        to_ascii(sum, buf[buf_len - 1 - 1]);

        *req = buf;
        return buf_len;
    }


    return 0;
}

static void send_request(u8 *s, u16 len)
{
    uart_send(s, len);
}

static void free_request(u8 *data)
{
    if (data) {
        free(data);
    }
}

bool fx_enquiry(void)
{
    bool ret = false;

    create_response(0);
    uart_send(ENQ);
    ret = wait_response(WAIT_RECV_TIMEOUT) && is_ack();
    free_response();

    return ret;
}

static bool fx_force_onoff(u8 addr_type, u16 addr, u8 cmd)
{
    register_t *r;
    u8 *req;
    u16 len;
    bool ret = false;

    if (fx_enquiry()) {
        r = find_registers(addr_type);
        if (r) {
            if ((len = create_request(r, cmd, addr, NULL, 0, &req)) > 0) {
                create_response(0);
                send_request(req, len);
                free_request(req);
                ret = wait_response(WAIT_RECV_TIMEOUT) && is_ack();
                free_response();
            }
        }
    }

    return ret;
}

bool fx_force_on(u8 addr_type, u16 addr)
{
    return fx_force_onoff(addr_type, addr, ACTION_FORCE_ON);
}

bool fx_force_off(u8 addr_type, u16 addr)
{
    return fx_force_onoff(addr_type, addr, ACTION_FORCE_OFF);
}

bool fx_read(u8 addr_type, u16 addr, u8 *out, u16 len)
{
    register_t *r;
    u8 *req;
    u16 len;
    bool ret = false;

    if (fx_enquiry()) {
        r = find_registers(addr_type);
        if (r) {
            if ((len = create_request(r, ACTION_READ, addr, NULL, 0, &req)) > 0) {
                create_response(0);
                send_request(req, len);
                free_request(req);
                ret = wait_response(WAIT_RECV_TIMEOUT) && is_ack();
                if (ret) {
                    copy_response_data(out, len);
                }
                free_response();
            }
        }
    }

    return ret;
}

static bool fx_write(u8 addr_type, u16 addr)
{
    register_t *r;
    u8 *req;
    u16 len;
    bool ret = false;

    if (fx_enquiry()) {
        r = find_registers(addr_type);
        if (r) {
            if ((len = create_request(r, ACTION_WRITE, addr, NULL, 0, &req)) > 0) {
                create_response(0);
                send_request(req, len);
                free_request(req);
                ret = wait_response(WAIT_RECV_TIMEOUT) && is_ack();
                free_response();
            }
        }
    }

    return ret;
}

