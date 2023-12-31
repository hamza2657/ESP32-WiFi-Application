/*
 * wifi_app.c
 *
 *  Created on: Sep 16, 2023
 *      Author: hamxa
 */

#include "wifi_app.h"
#include "app_nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "http_server.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "lwip/netdb.h"

#include "rgb_led.h"
#include "tasks_common.h"
#include "sdkconfig.h"
//tag used for esp serial consol message
static const char TAG[] = "wifi_app";

//wifi application callback
static wifi_connected_event_callback_t wifi_connected_event_cb;

//used for returning the wifi configuration
wifi_config_t *wifi_config = NULL;
//used to track number of retries
static int g_retry_number;

/**
 * Wifi application event group handle and status bits
 */
static EventGroupHandle_t wifi_app_event_group;
const int WIFI_APP_CONNECTING_USING_SAVED_CREDS_BIT			= BIT0;
const int WIFI_APP_CONNECTING_FROM_HTTP_SERVER_BIT			= BIT1; 
const int WIFI_APP_USER_REQUESTED_STA_DISCONNECT_BIT			= BIT2; 
const int WIFI_APP_STA_CONNECTED_GOT_IP_BIT					= BIT3; 


//Queue handle used to manipulate the main queue of events
static QueueHandle_t wifi_app_queue_handle;

//netif object for the satation and access point
esp_netif_t* esp_netif_sta = NULL;
esp_netif_t* esp_netif_ap = NULL;

/**
 * @fn void wifi_app_event_handler(void*, esp_event_base_t, int32_t, void*)
 * @brief wifi application event handler
 * 
 * @param arg aside from event data that is passed to the handler when it is called
 * @param event_base the base id of the event to register the handler for
 * @param event_id the id of the event to register the handler for
 * @param event_data event data
 */
static void wifi_app_event_handler(void *arg,esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	if (event_base == WIFI_EVENT)
	{
			switch (event_id)
			{
			case WIFI_EVENT_AP_START:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_START");
				break;

			case WIFI_EVENT_AP_STOP:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_STOP");
				break;

			case WIFI_EVENT_AP_STACONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");
				break;

			case WIFI_EVENT_AP_STADISCONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");
				break;

			case WIFI_EVENT_STA_START:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
				break;

			case WIFI_EVENT_STA_CONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
				break;

			case WIFI_EVENT_STA_DISCONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
				wifi_event_sta_disconnected_t *wifi_event_sta_disconnected = (wifi_event_sta_disconnected_t*)malloc(sizeof(wifi_event_sta_disconnected_t));
				*wifi_event_sta_disconnected = *((wifi_event_sta_disconnected_t*)event_data);
				printf("WIFI_EVENT_STA_DISCONNECTED, reason code %d\n", wifi_event_sta_disconnected->reason);

				if (g_retry_number < MAX_CONNECTION_RETRIES)
				{
					esp_wifi_connect();
						g_retry_number ++;
				}
				else
				{
					wifi_app_send_message(WIFI_APP_MSG_STA_DISCONNECTED);
				}

				break;
			}
		}
		else if (event_base == IP_EVENT)
		{
			switch (event_id)
			{
			case IP_EVENT_STA_GOT_IP:
				ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");

				wifi_app_send_message(WIFI_APP_MSG_STA_CONNECTED_GOT_IP);

				break;
			}
		}
}


/**
 * @fn  wifi_app_event_handler_init()
 * @brief initiliaze the wifi application event handler for wifi and IP event 
 * 
 */
static void wifi_app_event_handler_init(void)
{
	//event loop for the wifi driver
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	//Event handler for the connection
	esp_event_handler_instance_t instance_wifi_event;
	esp_event_handler_instance_t instance_ip_event;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID, &wifi_app_event_handler, NULL, &instance_wifi_event));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,ESP_EVENT_ANY_ID, &wifi_app_event_handler, NULL, &instance_ip_event));
}

/**
 * @fn void wifi_app_default_wifi_init(void)
 * @brief  initlize the TCP stack and default Wifi configuration
 * 
 */
static void wifi_app_default_wifi_init(void)
{
	//initilize the TCP stack
	ESP_ERROR_CHECK(esp_netif_init());
	
	//Default wifi configuration - operations must be in this order define below
	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	
	esp_netif_sta = esp_netif_create_default_wifi_sta();
	esp_netif_ap = esp_netif_create_default_wifi_ap();
}

/**
 * @fn void wifi_app_softAP_config(void)
 * @brief configure the wifi access point settings and assign the static IP to the soft AP
 * 
 */
static void wifi_app_softAP_config(void)
{
	//softAp -wifi access point configuration
	wifi_config_t ap_config = {
			.ap ={
					.ssid = WIFI_AP_SSID,
					.ssid_len = strlen(WIFI_AP_SSID),
					.password = WIFI_AP_PASSWORD,
					.channel = WIFI_AP_CHANNEL,
					.ssid_hidden = WIFI_AP_SSID_HIDDEN,
					.authmode = WIFI_AUTH_WPA2_PSK,
					.max_connection = WIFI_AP_MAX_CONNECTIONS,
					.beacon_interval = WIFI_AP_BEACON_INTERVAL,
					
			},
	};
	
	//configure DHCP for the AP
	esp_netif_ip_info_t ap_ip_info;
	memset(&ap_ip_info, 0x00, sizeof(ap_ip_info));
	esp_netif_dhcps_stop(esp_netif_ap); // must call this first
	inet_pton(AF_INET, WIFI_AP_IP, &ap_ip_info.ip); // assign access points static IP,GW and netmask
	inet_pton(AF_INET, WIFI_AP_GATEWAY, &ap_ip_info.gw);
	inet_pton(AF_INET, WIFI_AP_NETMASK, &ap_ip_info.netmask);
	ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ap_ip_info)); // statically configure the network interface
	ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap)); // start the AP GHCP server for connecting stations e.g mobile device
	
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA)); // setting the mode as AP and STA
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config)); //sets our configuration
	ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP,WIFI_AP_BANDWIDTH)); //our default bandwidth 20MHz
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_STA_POWER_SAVE)); //power save set to none
}
/**
 * @fn void wifi_app_connect_sta(void)
 * @brief connect the esp32 to an external ap using the updated station configuration 
 * 
 */
static void wifi_app_connect_sta(void)
{
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_app_get_wifi_config()));
	ESP_ERROR_CHECK(esp_wifi_connect());
}

/**
 * @fn void wifi_app_task(void*)
 * @brief Main task for wifi application
 * 
 * @param pvParameters
 */
static void wifi_app_task(void *pvParameters)
{
	wifi_app_queue_message_t msg;
	EventBits_t eventBits;
	
	//initlize event handler
	wifi_app_event_handler_init();
	
	//intialize the TCP/IP stack and wifi config
	wifi_app_default_wifi_init();
	
	//softAP config
	wifi_app_softAP_config();
	
	//start Wifi
	ESP_ERROR_CHECK(esp_wifi_start());
	
	//send first event message
	wifi_app_send_message(WIFI_APP_MSG_LOAD_SAVED_CREDENTIALS);
	
	for (;;)
		{
			if (xQueueReceive(wifi_app_queue_handle, &msg, portMAX_DELAY))
			{
				switch (msg.msgID)
				{
				case WIFI_APP_MSG_LOAD_SAVED_CREDENTIALS:
					ESP_LOGI(TAG, "WIFI_APP_MSG_LOAD_SAVED_CREDENTIALS");

					if (app_nvs_load_sta_creds())
					{
						ESP_LOGI(TAG, "Loaded station configuration");
						wifi_app_connect_sta();
						xEventGroupSetBits(wifi_app_event_group, WIFI_APP_CONNECTING_USING_SAVED_CREDS_BIT);
					}
					else
					{
						ESP_LOGI(TAG, "Unable to load station configuration");
					}

					// Next, start the web server
					wifi_app_send_message(WIFI_APP_MSG_START_HTTP_SERVER);

					break;

				case WIFI_APP_MSG_START_HTTP_SERVER:
					ESP_LOGI(TAG, "WIFI_APP_MSG_START_HTTP_SERVER");

					http_server_start();
					rgb_led_http_server_started();

					break;

				case WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER:
					ESP_LOGI(TAG, "WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER");

					xEventGroupSetBits(wifi_app_event_group, WIFI_APP_CONNECTING_FROM_HTTP_SERVER_BIT);

					// Attempt a connection
					wifi_app_connect_sta();

					// Set current number of retries to zero
					g_retry_number = 0;

					// Let the HTTP server know about the connection attempt
					http_server_monitor_send_message(HTTP_MSG_WIFI_CONNECT_INIT);

					break;

				case WIFI_APP_MSG_STA_CONNECTED_GOT_IP:
					ESP_LOGI(TAG, "WIFI_APP_MSG_STA_CONNECTED_GOT_IP");

					xEventGroupSetBits(wifi_app_event_group, WIFI_APP_STA_CONNECTED_GOT_IP_BIT);

					rgb_led_wifi_connected();
					http_server_monitor_send_message(HTTP_MSG_WIFI_CONNECT_SUCCESS);

					eventBits = xEventGroupGetBits(wifi_app_event_group);
					if (eventBits & WIFI_APP_CONNECTING_USING_SAVED_CREDS_BIT) ///> Save STA creds only if connecting from the http server (not loaded from NVS)
					{
						xEventGroupClearBits(wifi_app_event_group, WIFI_APP_CONNECTING_USING_SAVED_CREDS_BIT); ///> Clear the bits, in case we want to disconnect and reconnect, then start again
					}
					else
					{
						app_nvs_save_sta_creds();
					}

					if (eventBits & WIFI_APP_CONNECTING_FROM_HTTP_SERVER_BIT)
					{
						xEventGroupClearBits(wifi_app_event_group, WIFI_APP_CONNECTING_FROM_HTTP_SERVER_BIT);
					}

					// Check for connection callback
					if (wifi_connected_event_cb)
					{
						wifi_app_call_callback();
					}

					break;

				case WIFI_APP_MSG_USER_REQUESTED_STA_DISCONNECT:
					ESP_LOGI(TAG, "WIFI_APP_MSG_USER_REQUESTED_STA_DISCONNECT");

					eventBits = xEventGroupGetBits(wifi_app_event_group);

					if (eventBits & WIFI_APP_STA_CONNECTED_GOT_IP_BIT)
					{
						xEventGroupSetBits(wifi_app_event_group, WIFI_APP_USER_REQUESTED_STA_DISCONNECT_BIT);

						g_retry_number = MAX_CONNECTION_RETRIES;
						ESP_ERROR_CHECK(esp_wifi_disconnect());
						app_nvs_clear_sta_creds();
						rgb_led_http_server_started(); ///> todo: rename this status LED to a name more meaningful (to your liking)...
					}

					break;

				case WIFI_APP_MSG_STA_DISCONNECTED:
					ESP_LOGI(TAG, "WIFI_APP_MSG_STA_DISCONNECTED");

					eventBits = xEventGroupGetBits(wifi_app_event_group);
					if (eventBits & WIFI_APP_CONNECTING_USING_SAVED_CREDS_BIT)
					{
						ESP_LOGI(TAG, "WIFI_APP_MSG_STA_DISCONNECTED: ATTEMPT USING SAVED CREDENTIALS");
						xEventGroupClearBits(wifi_app_event_group, WIFI_APP_CONNECTING_USING_SAVED_CREDS_BIT);
						app_nvs_clear_sta_creds();
					}
					else if (eventBits & WIFI_APP_CONNECTING_FROM_HTTP_SERVER_BIT)
					{
						ESP_LOGI(TAG, "WIFI_APP_MSG_STA_DISCONNECTED: ATTEMPT FROM THE HTTP SERVER");
						xEventGroupClearBits(wifi_app_event_group, WIFI_APP_CONNECTING_FROM_HTTP_SERVER_BIT);
						http_server_monitor_send_message(HTTP_MSG_WIFI_CONNECT_FAIL);
					}
					else if (eventBits & WIFI_APP_USER_REQUESTED_STA_DISCONNECT_BIT)
					{
						ESP_LOGI(TAG, "WIFI_APP_MSG_STA_DISCONNECTED: USER REQUESTED DISCONNECTION");
						xEventGroupClearBits(wifi_app_event_group, WIFI_APP_USER_REQUESTED_STA_DISCONNECT_BIT);
						http_server_monitor_send_message(HTTP_MSG_WIFI_USER_DISCONNECT);
					}
					else
					{
						ESP_LOGI(TAG, "WIFI_APP_MSG_STA_DISCONNECTED: ATTEMPT FAILED, CHECK WIFI ACCESS POINT AVAILABILITY");
						// Adjust this case to your needs - maybe you want to keep trying to connect...
					}

					if (eventBits & WIFI_APP_STA_CONNECTED_GOT_IP_BIT)
					{
						xEventGroupClearBits(wifi_app_event_group, WIFI_APP_STA_CONNECTED_GOT_IP_BIT);
					}

					break;

				default:
					break;

				}
			}
		}
}

BaseType_t wifi_app_send_message(wifi_app_message_e msgID)
{
	wifi_app_queue_message_t msg;
	msg.msgID = msgID;
	return xQueueSend(wifi_app_queue_handle, &msg,portMAX_DELAY);
	
}

wifi_config_t* wifi_app_get_wifi_config(void)
{
	return wifi_config;
}


void wifi_app_set_callback(wifi_connected_event_callback_t cb)
{
	wifi_connected_event_cb = cb;
}

void wifi_app_call_callback(void)
{
	wifi_connected_event_cb();
}

int8_t wifi_app_get_rssi(void)
{
	wifi_ap_record_t wifi_data;
	ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&wifi_data));
	return wifi_data.rssi;
}

void wifi_app_start(void)
{
	ESP_LOGI(TAG,"STARTING WIFI APPLICATION");
	
	//start wifi started LED
	rgb_led_wifi_app_started();
	
	//Disable defualt Wifi logging messages
	esp_log_level_set("wifi", ESP_LOG_NONE);
	//Allocate memory for the wifi configuration
	wifi_config = (wifi_config_t*)malloc(sizeof(wifi_config_t));
	memset(wifi_config, 0x00,sizeof(wifi_config_t));
	
	//create queue message
	wifi_app_queue_handle = xQueueCreate(3,sizeof(wifi_app_queue_message_t));
	
	//create wifi application even group
	wifi_app_event_group = xEventGroupCreate();
	
	xTaskCreatePinnedToCore(&wifi_app_task, "wifi_app_task", WIFI_APP_TASK_STACK_SIZE, NULL, WIFI_APP_TASK_PRIORITY, NULL, WIFI_APP_TASK_CORE_ID);
}

