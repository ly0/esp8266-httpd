/******************************************************************************
 * Copyright 2013-2014 Espressif Systems (Wuxi)
 *
 * FileName: user_main.c
 *
 * Description: entry file of user application
 *
 * Modification history:
 *     2014/12/1, v1.0 create this file.
*******************************************************************************/
#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "udhcp/dhcpd.h"

#include "ets_sys.h"
#include "httpd.h"
#define server_ip "192.168.101.142"
#define server_port 9669

void smartlink_success(void *args)
{
    printf("test\n");
}

void smartlink_failed(void *args)
{
    printf("test\n");
}


void isr1(void)
{
    printf("\nPRESSED\n");
    printf("");
}
/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void
user_init(void)
{
    unsigned int gpio_status;
    uart_div_modify(0, UART_CLK_FREQ / 115200);
    u8_t *mem_ptr = (u8_t *)0x40020000;
    printf("\n\n\n");
    printf("mem_ptr: %x\n", mem_ptr);
    
    wifi_set_opmode(0x02);
    //udhcpd_start();
    
    {
        struct dhcp_info *pdhcp_info = NULL;
        pdhcp_info = (struct dhcp_info *)zalloc(sizeof(struct dhcp_info));
        pdhcp_info->start_ip = ipaddr_addr("192.168.145.100");
        pdhcp_info->end_ip = ipaddr_addr("192.168.145.110"); // don't set the range too large, because it will cost memory.
        pdhcp_info->max_leases = 10;
        pdhcp_info->auto_time = 60;
        pdhcp_info->decline_time = 60;
        pdhcp_info->conflict_time = 60;
        pdhcp_info->offer_time = 60;
        pdhcp_info->min_lease_sec = 60;
        dhcp_set_info(pdhcp_info);
        free(pdhcp_info);
    }
    udhcpd_start();
    
    //smartlink_init(smartlink_success, NULL, smartlink_failed,  5);
    //xTaskCreate(start_smarklink, "start_smarklink", 256, NULL, 2, NULL);

    httpd_init(mem_ptr);
    /*
    while(1)
    {

        printf("%x", GPIO_REG_READ(GPIO_STATUS_ADDRESS));
        vTaskDelay(500 / portTICK_RATE_MS);    
    }
    */
}

