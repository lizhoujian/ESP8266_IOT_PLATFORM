/******************************************************************************
 * Copyright (C) 2014 -2016  Espressif System
 *
 * FileName: user_uartovernet.c
 *
 * Description: Find your hardware's information while working any mode.
 *
 * Modification history:
 * 2015/7/1, v1.0 create this file.
*******************************************************************************/
#include "esp_common.h"

#include "driver/uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "user_uartovernet.h"
#include "user_config.h"

#define DEBUG

#ifdef DEBUG
#define DF_DEBUG os_printf
#else
#define DF_DEBUG
#endif

#define QUEUE_CMD_STOP 1 // exit recv task
#define QUEUE_CMD_PAUSE 2 // stop recv char from uart

LOCAL xQueueHandle QueueStop = NULL;
LOCAL xQueueHandle QueueStopUart = NULL;
LOCAL xQueueHandle QueueUart = NULL;
LOCAL xSemaphoreHandle xSemaphore = NULL;

#define UDF_SERVER_PORT 6001
#define len_udp_msg 128

LOCAL int32 sock_fd;
LOCAL struct sockaddr_in last_remote_addr = {0,};

LOCAL void set_remote_addr(struct sockaddr_in sa)
{
    xSemaphoreTake( xSemaphore, (portTickType)portMAX_DELAY );
    last_remote_addr = sa;
    xSemaphoreGive( xSemaphore );
}

LOCAL struct sockaddr_in get_remote_addr(void)
{
    struct sockaddr_in sa;
    xSemaphoreTake( xSemaphore, (portTickType)portMAX_DELAY );
    sa = last_remote_addr;
    xSemaphoreGive( xSemaphore );
    return sa;
}

LOCAL void
uart_recv_callback(u8 c)
{
    if (QueueUart) {
        xQueueSend(QueueUart, &c, 0);
    }
}

LOCAL void
user_uartovernet_data_process(char *pusrdata, unsigned short length, struct sockaddr_in *premote_addr)
{
    int i;

    if (pusrdata) {
        for (i = 0; i < length; i++) {
            uart_tx_one_char(UART0, pusrdata[i]);
        }
    }
}

LOCAL void
uart_recv_task(void *pvParameters)
{
    struct sockaddr_in remote_addr;
    u8 ValueFromReceive = 0;
    portBASE_TYPE xStatus;

    while (1) {
        xStatus = xQueueReceive(QueueStopUart, &ValueFromReceive, 0);
        if ( pdPASS == xStatus && QUEUE_CMD_STOP == ValueFromReceive) {
            os_printf("uart_recv_task rcv exit signal!\n");
            break;
        }
        xStatus = xQueueReceive(QueueUart, &ValueFromReceive, (portTickType)1000/portTICK_RATE_MS);
        if (pdPASS == xStatus) {
            remote_addr = get_remote_addr();
            if (remote_addr.sin_len > 0) {
                sendto(sock_fd, &ValueFromReceive, 1, 0, (struct sockaddr *)&remote_addr, sizeof(struct sockaddr_in));
            }
        }
    }

    vQueueDelete(QueueStopUart);
    QueueStopUart = NULL;
    vQueueDelete(QueueUart);
    QueueUart = NULL;
    vTaskDelete(NULL);
}

LOCAL void
user_uartovernet_task(void *pvParameters)
{
    struct sockaddr_in server_addr;
    int32 ret;
    struct sockaddr_in from;
    socklen_t fromlen;
    char  *udp_msg;
    u8 ValueFromReceive = 0;
    portBASE_TYPE xStatus;
    int bufSize = 0; // snd \recv buf size, set to zero for directly snd\recv
    int nNetTimeout = 10000;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(UDF_SERVER_PORT);
    server_addr.sin_len = sizeof(server_addr);
    udp_msg = (char *)malloc(len_udp_msg);

    do {
        sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
         if (sock_fd == -1) {
            os_printf("ERROR:devicefind failed to create sock!\n");
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
    } while (sock_fd == -1);

    do {
        ret = bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (ret != 0) {
            os_printf("ERROR:devicefind failed to bind sock!\n");
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
    } while (ret != 0);

    while (1) {
        xStatus = xQueueReceive(QueueStop, &ValueFromReceive, 0);
        if ( pdPASS == xStatus && QUEUE_CMD_STOP == ValueFromReceive) {
            os_printf("user_uartovernet_task rcv exit signal!\n");
            break;
        }
        memset(udp_msg, 0, len_udp_msg);
        memset(&from, 0, sizeof(from));
        setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&nNetTimeout, sizeof(int));
        setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, (const char*)&bufSize, sizeof(int));
        setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, (const char*)&bufSize, sizeof(int));
        fromlen = sizeof(struct sockaddr_in);
        ret = recvfrom(sock_fd, (u8 *)udp_msg, len_udp_msg, 0, (struct sockaddr *)&from, (socklen_t *)&fromlen);
        if (ret > 0) {
            os_printf("recieve from->port %d  %s, msg=%s\n", ntohs(from.sin_port), inet_ntoa(from.sin_addr), udp_msg);
            set_remote_addr(from);
            user_uartovernet_data_process(udp_msg, ret, &from);
        }
    }

    if (udp_msg)
        free(udp_msg);
    close(sock_fd);
    vQueueDelete(QueueStop);
    QueueStop = NULL;
    vTaskDelete(NULL);
}

void user_uartovernet_start(void)
{
    user_uartovernet_set_uart_cb();

    if (QueueStop == NULL)
        QueueStop = xQueueCreate(1, 1);
    if (QueueStopUart == NULL)
        QueueStopUart = xQueueCreate(1, 1);
    if (QueueUart == NULL)
        QueueUart = xQueueCreate(128, 1);

    if (!xSemaphore) {
        vSemaphoreCreateBinary(xSemaphore);
    }

    if (QueueStop != NULL)
        xTaskCreate(user_uartovernet_task, "user_uartovernet", 256, NULL, 3, NULL);
    if (QueueStopUart != NULL && QueueUart != NULL)
        xTaskCreate(uart_recv_task, "uart_recv_task", 256, NULL, 3, NULL);

    printf("user_uartovernet_start ok.\n");
}

sint8 user_uartovernet_stop(void)
{
    u8 ValueToSend = QUEUE_CMD_STOP;
    portBASE_TYPE xStatus;

    if (QueueStop == NULL || QueueStopUart == NULL)
        return -1;

    xStatus = xQueueSend(QueueStopUart, &ValueToSend, 0);
    if (xStatus != pdPASS) {
        os_printf("Could not send to the uart stop queue!\n");
        return -1;
    }

    xStatus = xQueueSend(QueueStop, &ValueToSend, 0);
    if (xStatus != pdPASS) {
        os_printf("Could not send to the task queue!\n");
        return -1;
    } else {
        while (QueueStop != NULL && QueueStopUart != NULL) {
            printf("user_uartovernet_stop, wait task exit.\n");
            taskYIELD();
        }
        vSemaphoreDelete(xSemaphore);
        xSemaphore = NULL;
        taskYIELD();
        return pdPASS;
    }
}

void user_uartovernet_set_uart_cb(void)
{
    uart_set_recv_cb(uart_recv_callback);
}

