/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
/***
 * 一键智能配网例程
 * 
 * *********************************************************************/
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"

/* FreeRTOS event group to signal when we are connected & ready to make a request。（FreeRTOS事件标志组发出信号，，在我们已经连接并准备发出请求时发出信号） */
static EventGroupHandle_t s_wifi_event_group;//定义事件组句柄

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP?  事件组允许每个事件有多个比特，但我们只关心一件事——我们之间有联系吗具有IP的AP？*/
static const int CONNECTED_BIT = BIT0;//连接成功标志位
static const int ESPTOUCH_DONE_BIT = BIT1;//ESPTOUCH完成标志位
static const char *TAG = "smartconfig_example";

static void smartconfig_example_task(void * parm);//配网任务

/****任务事件回调函数*******/
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    //wifi 事件，STA模式下且已经启动WIFI
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) { 
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);//创建配网连接任务
    //wifi 事件   STA模式下且已经断开    WIFI
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();//将wifi连接到AP
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);//清除联网成功标志位    显式清除事件组指定的位（任务中使用不能中断中使用，中断有专门函数） 事件标志组某个标志清零，参数1：事件组句柄，参数2：指定时间组中哪个位需要清零
    //对于那些基于 LwIP 构建的应用程序，此事件较为特殊，因为它意味着应用程序已准备就绪，可以开始任务，
    //例如：创建 TCP/UDP 套接字等。此时较为容易犯的一个错误就是在接收到 IP_EVENT_STA_GOT_IP 事件之前就初始化套接字。切忌在接收到 IP 之前启动任何套接字相关操作。
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);//置位联网标志位，置位事件组中指定的位（任务中使用不能中断中使用，中断有专门函数）  参数1：事件组句柄。参数2：指定事件中的事件标志位。
    //SC_EVENT事件，且一键配网完成扫描事件
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {     //
        ESP_LOGI(TAG, "Scan done");
    //SC_EVENT事件，且一键配网完成发现通道  
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
   
    }
     //SC_EVENT事件，且一键配网   获得SSID 和PSWD
    /**
     * @brief 【解析出WiFi的SSID和密码】 [esp_wifi_disconnect断开当前WiFi连接】
    【esp_wifi_set_config WiFI配置，设置WIFI_IF_STA模式，设置WiFi的SSID和密码】
     【esp_wifi_connect WiFi连接】
     * 
     */
     else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;//将无类型指针强制转换成需要的指针类型
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };
        uint8_t rvd_data[33] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));//bzero() 会将内存块（字符串）的前n个字节清零，其原型为： void bzero(void *s, int n);【参数】s为内存（字符串）指针，n 为需要清零的字节数。
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;//无论是否设置了目标AP的MAC地址。通常，station_config.bssid_set需要为0；并且仅当用户需要检查AP的MAC地址时才需要为1
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);
        //如果是在SC_TYPE_ESPTOUCH_V2配网模式下，打印输出收到的数据
        if (evt->type == SC_TYPE_ESPTOUCH_V2) {
            ESP_ERROR_CHECK( esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)) );
            ESP_LOGI(TAG, "RVD_DATA:");
            for (int i=0; i<33; i++) {
                printf("%02x ", rvd_data[i]);//%02X表示长度小于2用0填充，而不是截断为长度2
            }
            printf("\n");
        }

        ESP_ERROR_CHECK( esp_wifi_disconnect() );//断开当前wifi连接
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );//解析出的SSID和PSWD配置新的参数
        esp_wifi_connect();//连接到目标AP
    }
    //给App发送完成ACK 
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}
/**wifi初始化设置***/
static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());//初始化LwIP  初始化底层TCP/IP堆栈。这个函数应该在应用程序启动时从应用程序代码中精确地调用一次。
    s_wifi_event_group = xEventGroupCreate();//创建一个新的 RTOS 事件组，并返回 可以引用新创建的事件组的句柄。
    ESP_ERROR_CHECK(esp_event_loop_create_default());//创建默认事件循环
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();//创建默认WIFI STA   另一个是  esp_netif_create_default_wifi_ap 创建默认的WIFI AP
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();//
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );//WIFI驱动初始化

    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );//wifi 事件
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );//IP事件
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );//SC事件

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );//设置wifi工作模式
    ESP_ERROR_CHECK( esp_wifi_start() );//启动wifi
}

static void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS) );//SC_TYPE_ESPTOUCH支持ESPTOUCH配网，SC_TYPE_ESPTOUCH_AIRKISS支持ESPTOUCH和微信AIRKISS配网
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );//开启任务
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);//读取 RTOS 事件组中的位，选择性地进入“已阻塞”状态（已设置 超时值）以等待设置单个位或一组位。
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        //循环检测标志位，检测到 uxBits 为true 且 ESPTOUCH_DONE_BIT 标志位
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);//  删除任务
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi();
}
