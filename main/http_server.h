/*
 * http_server.h
 *
 *  Created on: Sep 27, 2023
 *      Author: hamxa
 */

#ifndef MAIN_HTTP_SERVER_H_
#define MAIN_HTTP_SERVER_H_

#define OTA_UPDATE_PENDING		0
#define OTA_UPDATE_SUCCESSFULL	1
#define OTA_UPDATE_FAILED		-1

/**
 * Message for HTTP Monitor
 */
typedef enum http_server_message
{
	HTTP_MSG_WIFI_CONNECT_INIT = 0,
	HTTP_MSG_WIFI_CONNECT_SUCCESS,
	HTTP_MSG_WIFI_CONNECT_FAIL,
	HTTP_MSG_OTA_UPDATE_SUCCESSFUL,
	HTTP_MSG_OTA_UPDATE_FAILED
}http_server_message_e;

/**
 * Structure for the message queue
 */
typedef struct http_server_queue_message{
	http_server_message_e msgID;
}http_server_queue_message_t;

/**
 * @fn BaseType_t http_server_monitor_send_message(http_server_message_e)
 * @brief send a message to the queue
 * 
 * @param msgID
 * @return pdTRUE if an item was successfully sent to the queue, other wise pdFALSE
 */
BaseType_t http_server_monitor_send_message(http_server_message_e msgID);

/**
 * @fn void http_server_start(void)
 * @brief start the http server
 * 
 */
void http_server_start(void);

/**
 * @fn void http_server_stop(void)
 * @brief stop the http server
 * 
 */
void http_server_stop(void);
/**
 * @fn void http_server_fw_update_reset_callback(void*)
 * @brief  timer callback function which will call esp_restart upon successful firmware update 
 * 
 * @param arg
 */
void http_server_fw_update_reset_callback(void *arg);


#endif /* MAIN_HTTP_SERVER_H_ */
