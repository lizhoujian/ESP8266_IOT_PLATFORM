#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32) || defined(_WIN64)
#define __WINDOWS__
#endif

#ifdef __WINDOWS__
#include <stdarg.h>
#include <stdio.h>
#include <windows.h>
#include <process.h>
#include "user_fx.h"
// test response for windows
//#define __WINDOWS_TEST__

typedef void *xQueueHandle;
typedef void *xSemaphoreHandle;
typedef unsigned int portTickType;

#define UART0 0

#define portTICK_RATE_MS 100
#define portMAX_DELAY 0
#define __xQueueCreate(A, B) NULL
#define __xQueueReset(A)
#define __xQueueSendFromISR(A, B, C)
#define __xQueueReceive(A, B, C)
#define ____xSemaphoreTakeFromISR(A, B)
#define ____xSemaphoreGiveFromISR(A, B)
#define __xSemaphoreTake(A, B)
#define __xSemaphoreGive(A)
#define __xSemaphoreCreateMutex() NULL
#define __vQueueDelete(A)

static void uart_init_for_fx(void)
{
}

static set_tx_cb curr_set_tx_cb;
void uart_set_tx_cb(set_tx_cb cb)
{
    curr_set_tx_cb = cb;
}

static void uart_cb(u8 c);
void uart_on_recv_char(unsigned char c)
{
    uart_cb((u8)c);
}

static void uart_tx_one_char(u8 port, u8 c)
{
    if (curr_set_tx_cb)
    {
        (*curr_set_tx_cb)((unsigned char)c);
    }
}

typedef void (*cb)(u8 c);
static void uart_set_recv_cb(cb myCb)
{
}

static set_tx_string_cb curr_set_tx_string_cb;
void uart_set_tx_string_cb(set_tx_string_cb cb)
{
    curr_set_tx_string_cb = cb;
}
static void uart_send(u8 *data, u16 len)
{
    int i;
    if (curr_set_tx_string_cb)
    {
        (*curr_set_tx_string_cb)(data, len);
    }
    else
    {
        for (i = 0; i < len; i++)
        {
            uart_tx_one_char(0, data[i]);
        }
    }
}

static void TRACE(const char *sz, ...)
{
    char szData[512] = {0};
    va_list args;
    va_start(args, sz);
    _vsnprintf(szData, sizeof(szData) - 1, sz, args);
    va_end(args);
    OutputDebugString(szData);
}
#else // __windows__
//#define __ESP_TEST__
#include "esp_common.h"
#include "c_types.h"
#include "user_config.h"
#include "driver/uart.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "user_fx.h"
#define TRACE printf

#define __xQueueCreate(A, B) NULL
#define __xQueueReset(A)
#define __xQueueSendFromISR(A, B, C)
#define __xQueueReceive(A, B, C)
#define ____xSemaphoreTakeFromISR(A, B)
#define ____xSemaphoreGiveFromISR(A, B)
#define __xSemaphoreTake(A, B)
#define __xSemaphoreGive(A)
#define __xSemaphoreCreateMutex() NULL
#define __vQueueDelete(A)

static void uart_cb(u8 c);
void uart_on_recv_char(unsigned char c)
{
    uart_cb((u8)c);
}
static void uart_send(u8 *data, u16 len)
{
    int i;
#ifdef __ESP_TEST__
    TRACE("uart send request: \n");
    for (i = 0; i < len; i++)
    {
        TRACE("%02x ", data[i]);
    }
    TRACE("\n");
#else
    for (i = 0; i < len; i++)
    {
        uart_tx_one_char(UART0, data[i]);
    }
#endif
}
#endif

#include "user_fx.h"

#define __countof(a) (sizeof(a) / sizeof(a[0]))

#define TO_ASCII(half) (((half) < 10) ? ((half) + 0x30) : ((half) + 0x41 - 0xa))
#define SWAP_BYTE(word) ((((word) >> 8) & 0xff) | (((word) << 8) & 0xff00));

#define TO_HEX(c) ((((c) - 0x30) < 10) ? ((c) - 0x30) : ((c) - 0x41 + 10))

typedef struct register_t
{
    u8 type; /* register type */
    u32 byte_base_addr; /* base address for current */
    u32 bit_base_addr; /* base address for current */
    u32 (*addr)(void *r, u16 offset, bool bit); /* calc address */
    u8 unit_len; /* bytes for per address */
    u32 funcs; /* support functions force on\off, read\write */
    bool write_by_bit; /* write operate replace by bit operate */
    u32 bits;
} register_t;

#define WAIT_RECV_TIMEOUT 3000

typedef struct uart_event_t
{
    u8 event;
} uart_event_t;
enum
{
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
static uart_buf_t uart_recv_buf = {0,};

static void post_event(u8 ev)
{
    uart_event_t e = {0,};
    e.event = ev;
    __xQueueSendFromISR(uart_queue_recv, (void *)&e, NULL);
}

static void parse_buf(uart_buf_t *p, u16 len)
{
    if (p->data && len == p->len || p->data[0] == NAK)
    {
        post_event(UART_EVENT_DONE);
    }
}

static void uart_cb(u8 c)
{
    uart_buf_t *p = &uart_recv_buf;
    ____xSemaphoreTakeFromISR(uart_queue_recv, NULL);
    if (p->data && p->index < p->len)
    {
        p->data[p->index++] = c;
        parse_buf(p, p->index);
    }
    else
    {
        TRACE("overflow or not init c=%x, index=%d, len=%d ???\n", c, p->index, p->len);
    }
    ____xSemaphoreGiveFromISR(uart_queue_recv, NULL);
}

u8 check_sum(u8 *in, u16 inLen)
{
    int i, sum;
    sum = 0;
    for (i = 0; i < inLen; i++)
    {
        sum += in[i];
    }
    return sum & 0xff;
}

static u8 to_hex(u8 *in)
{
    return ((TO_HEX(in[0]) << 4) & 0xf0) | ((TO_HEX(in[1]) & 0xf));
}

static void ascii_to_hex(u8 *in, u8 *out, u16 inLen)
{
    int i;
    for (i = 0; i < inLen; i += 2)
    {
        out[i / 2] = to_hex(&in[i]);
    }
}

static void to_ascii(u8 in, u8 *out)
{
    out[0] = TO_ASCII((in >> 4) & 0xf);
    out[1] = TO_ASCII(in & 0xf);
}

void hex_to_ascii(u8 *in, u8 *out, u16 inLen)
{
    int i;
    for (i = 0; i < inLen; i++)
    {
        to_ascii(in[i], out + i * 2);
    }
}

static void free_recv_buff(void)
{
    uart_buf_t *p = &uart_recv_buf;
    __xSemaphoreTake(uart_queue_recv, portMAX_DELAY);
    if (p->data)
    {
        p->index = 0;
        p->len = 0;
        free(p->data);
        p->data = NULL;
        //TRACE("free last recv buff.\n");
    }
    __xSemaphoreGive(uart_queue_recv);
}

static bool alloc_recv_buff(u16 len)
{
    uart_buf_t *p = &uart_recv_buf;
    __xSemaphoreTake(uart_queue_recv, portMAX_DELAY);
    p->len = len;
    p->data = (u8 *)malloc(p->len);
    if (p->data)
    {
        memset(p->data, 0, p->len);
    }
    p->index = 0;
    __xSemaphoreGive(uart_queue_recv);
    return p->data != NULL;
}

static bool wait_recv_done(u16 miliseconds)
{
    bool ret = false;
    int i;
    uart_buf_t *p = &uart_recv_buf;
    uart_event_t e = {0,};
    if ((miliseconds % (portTICK_RATE_MS)) > 0)
    {
        miliseconds += portTICK_RATE_MS;
    }
#ifdef __WINDOWS__
    miliseconds /= 5;
    while (miliseconds-- != 0)
    {
        _sleep(5);
        if (p->data && (p->index == p->len || p->data[0] == NAK || p->data[0] == ACK))
        {
            ret = true;
            break;
        }
    }
    ret = true;
#else
#if 0
    __xQueueReceive(uart_queue_recv, (void *)&e, (portTickType)(miliseconds / portTICK_RATE_MS));
    if (e.event == UART_EVENT_DONE)
    {
        ret = true;
    }
    else
    {
        TRACE("wait recv error(timeout?), event = %d\n", e.event);
        ret = false;
    }
#else
    miliseconds /= 10;
    while (miliseconds-- != 0)
    {
        vTaskDelay(1);
        if (p->data && (p->index == p->len || p->data[0] == NAK || p->data[0] == ACK))
        {
            ret = true;
            break;
        }
    }
    ret = true;
#endif
#endif
    TRACE("fx recv: ");
    for (i = 0; i < p->len; i++)
    {
        TRACE("%02x ", p->data[i]);
    }
    TRACE("\r\n");
    return ret;
}

static bool is_ack(void)
{
    uart_buf_t *p = &uart_recv_buf;
    u8 ack = 0;
    __xSemaphoreTake(uart_queue_recv, portMAX_DELAY);
    if (p->data)
    {
        ack = p->data[0];
    }
    __xSemaphoreGive(uart_queue_recv);
    return ack == ACK;
}

static bool is_stx(void)
{
    uart_buf_t *p = &uart_recv_buf;
    u8 stx = 0;
    __xSemaphoreTake(uart_queue_recv, portMAX_DELAY);
    if (p->data)
    {
        stx = p->data[0];
    }
    __xSemaphoreGive(uart_queue_recv);
    return stx == STX;
}

/* init response before request */
static bool create_response(u8 cmd, u16 data_len)
{
    u16 len;
    __xQueueReset(uart_queue_recv);
    if (cmd == ACTION_READ)
    {
        len = 1 + data_len * 2 + 1 + 2;
    }
    else
    {
        len = 1;
    }
    return alloc_recv_buff(len);
}

static bool wait_response(u16 miliseconds)
{
    return wait_recv_done(miliseconds);
}

static bool parse_response_data(u8 *out, u16 len)
{
    bool ret = false;
    uart_buf_t *p = &uart_recv_buf;
    u8 sum, recv_sum;
    __xSemaphoreTake(uart_queue_recv, portMAX_DELAY);
    if (p->data != NULL && p->index == p->len)
    {
        if (p->data[0] == ACK || p->data[0] == NAK)
        {
            return p->data[0] == ACK;
        }
        else if (p->data[0] == STX && (p->index > 3 && p->data[p->index - 1 - 2] == ETX))
        {
            sum = check_sum(&p->data[1], p->len - 3); /* - STX - CHECKSUM */
            recv_sum = to_hex(&p->data[p->index - 1 - 1]);
            if (sum == recv_sum)
            {
                ascii_to_hex(&p->data[1], out, p->len - 4);
                ret = true;
            }
            else
            {
                TRACE("response data check sum error.\n");
            }
        }
        else
        {
            TRACE("parse response data invalid.\n");
        }
    }
    else
    {
        TRACE("parse response data invalid.\n");
    }
    __xSemaphoreGive(uart_queue_recv);
    return ret;
}

static void free_response(void)
{
    free_recv_buff();
}

void fx_init(void)
{
#if 0
    uart_queue_recv = __xQueueCreate(1, sizeof(uart_event_t));
    if (uart_queue_recv)
    {
        recv_buf_mutex = __xSemaphoreCreateMutex();
        if (recv_buf_mutex)
        {
            uart_init_for_fx();
            uart_set_recv_cb(uart_cb);
        }
        else
        {
            __vQueueDelete(uart_queue_recv);
            TRACE("create recv_buf_mutex failed.\n");
        }
    }
    else
    {
        TRACE("create uart_queue_recv failed.\n");
    }
#else
    uart_init_for_fx();
    uart_set_recv_cb(uart_cb);
#endif
    TRACE("fx init ok.\n");
}

void fx_uart_take(void)
{
    UART_WaitTxFifoEmpty(UART0);
    UART_SetPrintPort(UART1);
    uart_set_recv_cb(uart_cb);
}

void fx_uart_release(void)
{
    UART_WaitTxFifoEmpty(UART0);
    UART_SetPrintPort(UART0);
    uart_set_recv_cb(NULL);
}

static u32 get_base(void *r, bool bit)
{
    register_t *t = (register_t *)r;
    return (bit ? t->bit_base_addr : t->byte_base_addr);
}
static u8 get_unit_len(void *r)
{
    register_t *t = (register_t *)r;
    return t->unit_len;
}

static u32 calc_address(void *r, u16 offset, bool bit)
{
    return get_base(r, bit) + offset * get_unit_len(r);
}

#define FUNCTION_FORCE_ON (1 << ACTION_FORCE_ON)
#define FUNCTION_FORCE_OFF (1 << ACTION_FORCE_OFF)
#define FUNCTION_READ (1 << ACTION_READ)
#define FUNCTION_WRITE (1 << ACTION_WRITE)

#define FUNCTION_OF (FUNCTION_FORCE_ON | FUNCTION_FORCE_OFF)
#define FUNCTION_WR (FUNCTION_READ | FUNCTION_WRITE)
#define FUNCTION_ALL (FUNCTION_OF | FUNCTION_WR)

static register_t registers[] =
{
    {REG_S, REG_S_BASE_ADDRESS, REG_S_BIT_BASE_ADDRESS, calc_address, 1, FUNCTION_ALL, true, REG_S_BITS},
    {REG_X, REG_X_BASE_ADDRESS, REG_X_BIT_BASE_ADDRESS, calc_address, 1, FUNCTION_READ, false, REG_X_BITS},
    {REG_Y, REG_Y_BASE_ADDRESS, REG_Y_BIT_BASE_ADDRESS, calc_address, 1, FUNCTION_ALL, true, REG_Y_BITS},
    {REG_T, REG_T_BASE_ADDRESS, REG_T_BIT_BASE_ADDRESS, calc_address, 1, FUNCTION_ALL, true, REG_T_BITS},
    {REG_M, REG_M_BASE_ADDRESS, REG_M_BIT_BASE_ADDRESS, calc_address, 1, FUNCTION_ALL, true, REG_M_BITS},
    {REG_C, REG_C_BASE_ADDRESS, REG_C_BIT_BASE_ADDRESS, calc_address, 1, FUNCTION_ALL, true, REG_C_BITS},
    {REG_MS, REG_MS_BASE_ADDRESS, REG_MS_BIT_BASE_ADDRESS, calc_address, 1, FUNCTION_ALL, false, REG_MS_BITS},
    {REG_D, REG_D_BASE_ADDRESS, REG_INVALID_ADDRESS, calc_address, 2, FUNCTION_WR, false, REG_D_BITS},
    {REG_YP, REG_YP_BASE_ADDRESS, REG_INVALID_ADDRESS, calc_address, 1, FUNCTION_WR, false, REG_YP_BITS},
    {REG_TO, REG_TO_BASE_ADDRESS, REG_INVALID_ADDRESS, calc_address, 1, FUNCTION_WR, false, REG_TO_BITS},
    {REG_MP, REG_MP_BASE_ADDRESS, REG_INVALID_ADDRESS, calc_address, 1, FUNCTION_WR, false, REG_MP_BITS},
    {REG_CO, REG_CO_BASE_ADDRESS, REG_INVALID_ADDRESS, calc_address, 1, FUNCTION_WR, false, REG_CO_BITS},
    {REG_TR, REG_TR_BASE_ADDRESS, REG_INVALID_ADDRESS, calc_address, 1, FUNCTION_WR, false, REG_TR_BITS},
    {REG_CR, REG_CR_BASE_ADDRESS, REG_INVALID_ADDRESS, calc_address, 1, FUNCTION_WR, false, REG_CR_BITS},
    {REG_TV16, REG_TV16_BASE_ADDRESS, REG_INVALID_ADDRESS, calc_address, 2, FUNCTION_WR, false, REG_TV16_BITS},
    {REG_CV16, REG_CV16_BASE_ADDRESS, REG_INVALID_ADDRESS, calc_address, 2, FUNCTION_WR, false, REG_CV16_BITS},
    {REG_CV32, REG_CV32_BASE_ADDRESS, REG_INVALID_ADDRESS, calc_address, 4, FUNCTION_WR, false, REG_CV32_BITS},
    {REG_DS, REG_DS_BASE_ADDRESS, REG_INVALID_ADDRESS, calc_address, 2, FUNCTION_WR, false, REG_DS_BITS},
};

static register_t *find_registers(u8 addr_type)
{
    int i, c = __countof(registers);
    for (i = 0; i < c; i++)
    {
        if (addr_type == registers[i].type)
            return &registers[i];
    }
    TRACE("not found register type.");
    return NULL;
}

u16 fx_unit_len(u8 addr_type)
{
    register_t *r = find_registers(addr_type);
    if (r)
    {
        return r->unit_len;
    }
    else
    {
        return 0;
    }
}

static bool is_little_endian(void)
{
    u16 d = 0x1234;
    u8 *c = (u8 *)&d;
    return *c == 0x34;
}

static u16 unit_addr(register_t *r, u8 cmd, u16 addr)
{
    u16 raddr = 0;
    if (cmd == ACTION_FORCE_ON || cmd == ACTION_FORCE_OFF)
    {
        raddr = (u16)r->addr(r, addr, true);
        if (!is_little_endian())
        {
            raddr = SWAP_BYTE(raddr);
        }
    }
    else if (cmd == ACTION_READ || cmd == ACTION_WRITE)
    {
        raddr = (u16)r->addr(r, addr, false);
        if (is_little_endian())
        {
            raddr = SWAP_BYTE(raddr);
        }
    }
    return raddr;
}

static u16 create_request(register_t *r, u8 cmd, u16 addr, u8 *data, u16 len, u8 **req)
{
    u8 *buf;
    u16 rlen;
    u16 raddr;
    u8 sum;
    raddr = unit_addr(r, cmd, addr);
    if (raddr == REG_INVALID_ADDRESS)
    {
        TRACE("reg type = %d, cmd = %d, invalid base address.\n", r->type, cmd);
        return 0;
    }
    rlen = 1 + 1 + 4;
    rlen += (len > 0 ? 2 : 0);
    rlen += (data != NULL ? len * 2 : 0);
    rlen += 1 + 2;
    buf = (u8 *)malloc(rlen);
    if (buf)
    {
        memset(buf, 0, rlen);
        buf[0] = STX;
        buf[1] = TO_ASCII(cmd);
        hex_to_ascii((u8 *)&raddr, &buf[2], 2); /* 4 bytes */
        if (len > 0)
        {
            to_ascii((u8)len, &buf[6]); /* 2 bytes */
        }
        if (data != NULL)
        {
            hex_to_ascii(data, &buf[8], len); /* (2 * len) bytes */
        }
        buf[rlen - 1 - 2] = ETX;
        sum = check_sum(&buf[1], rlen - 3); /* - STX - CHECKSUM */
        to_ascii(sum, &buf[rlen - 1 - 1]);
        *req = buf;
        return rlen;
    }
    return 0;
}

static void send_request(u8 *s, u16 len)
{
    uart_send(s, len);
}

static void free_request(u8 *data)
{
    if (data)
    {
        free(data);
    }
}

#ifdef __WINDOWS_TEST__
static unsigned int __stdcall thread_ack(void *param)
{
    uart_on_recv_char(ACK);
    return 0;
}
static unsigned int __stdcall thread_read(void *param)
{
    u16 rlen, len = (u16)param;
    u8 *buf, sum;
    int i, r;
    srand((unsigned)time(NULL));
    rlen = 1 + len * 2 + 1 + 2;
    buf = (u8 *)malloc(rlen);
    if (buf)
    {
        memset(buf, 0, rlen);
        buf[0] = STX;
        for (i = 0; i < len * 2; i++)
        {
            r = -1;
            while (!(r >= 0 && r <= 15))
            {
                r = rand() % 16;
            }
            buf[i + 1] = TO_ASCII((u8)r);
        }
        buf[rlen - 1 - 2] = ETX;
        sum = check_sum(&buf[1], rlen - 3); /* - STX - CHECKSUM */
        to_ascii(sum, &buf[rlen - 1 - 1]);
        for (i = 0; i < rlen; i++)
        {
            uart_on_recv_char(buf[i]);
        }
        free(buf);
    }
    return 0;
}
static void create_thread(unsigned int (__stdcall *t)(void *p), void *p)
{
    _beginthreadex(NULL, 0, t, p, 0, NULL);
}
#endif
#ifdef __ESP_TEST__
static unsigned int thread_ack(void *param)
{
    uart_on_recv_char(ACK);
    return 0;
}
static unsigned int thread_read(void *param)
{
    u16 rlen, len = (u16)param;
    u8 *buf, sum;
    int i, r;
    rlen = 1 + len * 2 + 1 + 2;
    buf = (u8 *)malloc(rlen);
    if (buf)
    {
        memset(buf, 0, rlen);
        buf[0] = STX;
        for (i = 0; i < len * 2; i++)
        {
            r = -1;
            while (!(r >= 0 && r <= 15))
            {
                r = (os_random()) % 16;
            }
            buf[i + 1] = TO_ASCII((u8)r);
        }
        buf[rlen - 1 - 2] = ETX;
        sum = check_sum(&buf[1], rlen - 3); /* - STX - CHECKSUM */
        to_ascii(sum, &buf[rlen - 1 - 1]);
        for (i = 0; i < rlen; i++)
        {
            uart_on_recv_char(buf[i]);
        }
        free(buf);
    }
    return 0;
}
static void create_thread(unsigned int (*t)(void *p), void *p)
{
    (*t)(p);
}
#endif

static bool has_cmd(register_t *r, u8 cmd)
{
    return ((1 << cmd) & r->funcs) != 0;
}

static bool fx_execute(u8 addr_type, u8 cmd, u16 addr, u8 *inout, u16 inout_len)
{
    register_t *r;
    u8 *req;
    u16 rlen;
    bool ret = false;
    //if (fx_enquiry()) {
    r = find_registers(addr_type);
    if (r && has_cmd(r, cmd))
    {
        if ((rlen = create_request(r, cmd, addr, (cmd == ACTION_WRITE ? inout : NULL), inout_len, &req)) > 0)
        {
            create_response(cmd, inout_len);
            send_request(req, rlen);
#if defined(__WINDOWS_TEST__) || defined(__ESP_TEST__)
            if (cmd == ACTION_READ)
            {
                create_thread(thread_read, (void *)inout_len);
            }
            else
            {
                create_thread(thread_ack, NULL);
            }
#endif
            free_request(req);
            ret = wait_response(WAIT_RECV_TIMEOUT);
            if (ret)
            {
                ret = parse_response_data(inout, inout_len);
            }
            free_response();
        }
    }
    //}
    return ret;
}

#define DIFF_BIT(a, b, i) ((((a) >> i) & 0x1) != (((b) >> i) & 0x1))

static bool write_byte(u8 addr_type, u8 old, u8 new, u16 addr)
{
    int i;
    for (i = 0; i < 8; i++)
    {
        if (DIFF_BIT(old, new, i))
        {
            if ((new >> i) & 0x1)
            {
                if (!fx_force_on(addr_type, addr * 8 + i))
                {
                    return false;
                }
            }
            else
            {
                if (!fx_force_off(addr_type, addr * 8 + i))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool fx_write_by_bit(u8 addr_type, u16 addr, u8 *data, u16 len)
{
    int i;
    bool ret = false;
    u8 *old;
    old = (u8 *)malloc(len + 1);
    if (!old)
    {
        return false;
    }
    memset(old, 0, len + 1);
    if (!fx_read(addr_type, addr, old, len))
    {
        goto __exit;
    }
    for (i = 0; i < len; i++)
    {
        if (old[i] != data[i])
        {
            if (!write_byte(addr_type, old[i], data[i], addr + i))
            {
                goto __exit;
            }
        }
    }
    ret = true;
__exit:
    free(old);
    return ret;
}

u32 fx_reg_bits(u8 addr_type)
{
    register_t *r;
    r = find_registers(addr_type);
    if (r) {
        return r->bits;
    } else {
        return 0;
    }
}

bool fx_enquiry(void)
{
    bool ret = false;
    u8 cmd = ENQ;
    create_response(cmd, 0);
    uart_send(&cmd, 1);
#if defined(__WINDOWS_TEST__) || defined(__ESP_TEST__)
    create_thread(thread_ack, NULL);
#endif
    ret = wait_response(WAIT_RECV_TIMEOUT) && is_ack();
    free_response();
    return ret;
}

bool fx_force_on(u8 addr_type, u16 addr)
{
    return fx_execute(addr_type, ACTION_FORCE_ON, addr, NULL, 0);
}

bool fx_force_off(u8 addr_type, u16 addr)
{
    return fx_execute(addr_type, ACTION_FORCE_OFF, addr, NULL, 0);
}

bool fx_read(u8 addr_type, u16 addr, u8 *out, u16 len)
{
    return fx_execute(addr_type, ACTION_READ, addr, out, len);
}

bool fx_write(u8 addr_type, u16 addr, u8 *data, u16 len)
{
    register_t *r;
    r = find_registers(addr_type);
    if (r && r->write_by_bit)
    {
        return fx_write_by_bit(addr_type, addr, data, len);
    }
    else
    {
        return fx_execute(addr_type, ACTION_WRITE, addr, data, len);
    }
}
