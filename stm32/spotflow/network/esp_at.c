#include "esp_at.h"
#include <string.h>
#include <stdio.h>
#include "spotflow.h"
#include "spotflow_config.h"

extern UART_HandleTypeDef huart1;

char cmd[256];

/**
 * @brief Sends a raw command to the ESP module
 *
 * @param cmd
 */
static void esp_send_raw(const char *cmd)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 1000);
}

/**
 * @brief Wait for a specific response from the ESP module within a timeout
 * @param expected
 * @param timeout
 * @return int
 */
static int esp_wait_for(const char *expected, uint32_t timeout)
{
    uint8_t ch;
    char buffer[128] = {0};
    uint16_t idx = 0;

    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout)
    {
	if (HAL_UART_Receive(&huart1, &ch, 1, 10) == HAL_OK)
	{
	    if (idx < sizeof(buffer) - 1)
	    {
		buffer[idx++] = ch;
		buffer[idx] = '\0';
	    }

	    if (strstr(buffer, expected))
		return 0;
	}
    }

    return -1;
}

/**
 * @brief Initiliza
 *
 */
void ESP_Init(void)
{
    ESP_SendCommand("AT", "OK", 10);
    ESP_SendCommand("ATE0", "OK", 10);
}

/**
 * @brief Sending the command to the ESP module and wait for the expected response within a timeout
 * @param cmd
 * @param expected
 * @param timeout
 * @return int
 */
int ESP_SendCommand(char *cmd, char *expected, uint32_t timeout)
{
    esp_send_raw(cmd);
    esp_send_raw("\r\n");

    return esp_wait_for(expected, timeout);
}

/**
 * @brief Wifi connection settings
 *
 * @param ssid
 * @param pass
 * @return int
 */
int ESP_WiFi_Connect(char *ssid, char *pass)
{

    /* Set station mode */
    if (ESP_SendCommand("AT+CWMODE=1", "OK", 2000) != 0)
	return -1;

    /* Enable auto reconnect on power-up */
    if (ESP_SendCommand("AT+CWAUTOCONN=1", "OK", 2000) != 0)
	return -1;

    /* Auto reconnect after disconnect */
    if (ESP_SendCommand("AT+CWRECONNCFG=1,5", "OK", 2000) != 0)
	return -1;

    /* Connect to AP */
    snprintf(cmd, sizeof(cmd),
	     "AT+CWJAP=\"%s\",\"%s\"",
	     ssid, pass);

    return ESP_SendCommand(cmd, "WIFI CONNECTED", 15000);
}

/**
 * @brief MQTT connection settings
 *
 * @param client_id
 * @param username
 * @param password
 * @return int
 */
int ESP_MQTT_Connect(char *client_id, char *username, char *password)
{
	// Update Time on the board
	if (ESP_SendCommand("AT+CIPSNTPCFG=1,0,\"pool.ntp.org\",\"0.pool.ntp.org\"", "TIME_UPDATED", 2000) != 0)
		return -1;
    /* Upload cert (only needed once ideally, but safe here for now) */
	ESP_UploadChunk(MQTT_CA_NAME,
    		(const uint8_t *)ROOT_CA_PEM,
    		    strlen(ROOT_CA_PEM));

    /* Configure MQTT with cert */
    snprintf(cmd, sizeof(cmd),
	     "AT+MQTTUSERCFG=0,2,\"%s\",\"%s\",\"%s\",0,0,\"%s.0\"",
	     client_id,
	     username ? username : "",
	     password ? password : "",
	     MQTT_CA_NAME);

    if (ESP_SendCommand(cmd, "OK", 3000) != 0)
	return -1;

    /* Connect with SSL enabled */
    snprintf(cmd, sizeof(cmd),
	     "AT+MQTTCONN=0,\"%s\",%d,%d",
	     SPOTFLOW_MQTT_URL,
	     SPOTFLOW_MQTT_PORT,
	     MQTT_USE_SSL);

    return ESP_SendCommand(cmd, "MQTTCONNECTED", 8000);
}

/* =========================
   MQTT Publish RAW (CBOR Safe)
   ========================= */

int ESP_MQTT_Publish(char *topic, uint8_t *payload, uint16_t len)
{
    uint8_t ch = 0;
    char resp[64] = {0};
    uint16_t idx = 0;

    /* Step 1: Send command */
    snprintf(cmd, sizeof(cmd),
	     "AT+MQTTPUBRAW=0,\"%s\",%d,0,0",
	     topic, len);

    esp_send_raw(cmd);
    esp_send_raw("\r\n");

    /* Step 2: Wait for '>' */
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 3000)
    {
	if (HAL_UART_Receive(&huart1, &ch, 1, 10) == HAL_OK)
	{
	    if (ch == '>')
		break;
	}
    }

    if (ch != '>')
	return -1;

    /* Step 3: Send payload */
    if (HAL_UART_Transmit(&huart1, payload, len, 5000) != HAL_OK)
	return -1;

    /* Step 4: Wait for OK */
    idx = 0;
    memset(resp, 0, sizeof(resp));

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 5000)
    {
	if (HAL_UART_Receive(&huart1, &ch, 1, 10) == HAL_OK)
	{
	    if (idx < sizeof(resp) - 1)
	    {
		resp[idx++] = ch;
		resp[idx] = '\0';
	    }

	    if (strstr(resp, "OK"))
		return 0;

	    if (strstr(resp, "ERROR"))
		return -1;
	    if (strstr(resp, "FAIL"))
		return -1;
	}
    }

    return -1;
}

//int ESP_UploadChunk(const char *name, const uint8_t *data, uint32_t len)
//{
//    uint8_t ch;
//
//    snprintf(cmd, sizeof(cmd),
//             "AT+SYSFLASH=0,\"%s\",%lu,1000",
//             name, (unsigned long)len);
//
//    esp_send_raw(cmd);
//    esp_send_raw("\r\n");
//
//    /* Wait for '>' */
//    uint32_t start = HAL_GetTick();
//    while ((HAL_GetTick() - start) < 3000)
//    {
//        if (HAL_UART_Receive(&huart1, &ch, 1, 10) == HAL_OK)
//        {
//            if (ch == '>')
//                break;
//        }
//    }
//
//    if (ch != '>')
//        return -1;
//
//    /* Send cert */
//    if (HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 5000) != HAL_OK)
//        return -1;
//
//    /* Wait for OK */
//    return esp_wait_for("OK", 5000);
//}

#define CHUNK_SIZE 256
int ESP_UploadChunk(const char *name, const uint8_t *data, uint32_t len)
{
    uint8_t ch;

    /* Prepare AT command */
    snprintf(cmd, sizeof(cmd), "AT+SYSMFG=2,\"%s\",\"%s.0\",7,%lu", name, (unsigned long)len);

    /* Send AT command */
    esp_send_raw(cmd);
    esp_send_raw("\r\n");

    /* Wait for '>' prompt */
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 3000)
    {
	if (HAL_UART_Receive(&huart1, &ch, 1, 10) == HAL_OK)
	{
	    if (ch == '>')
		break;
	}
    }

    if (ch != '>')
	return -1;  // prompt not received

    /* Send certificate in chunks */
    for (uint32_t offset = 0; offset < len; offset += CHUNK_SIZE)
    {
	uint32_t send_len = (len - offset > CHUNK_SIZE) ? CHUNK_SIZE : (len - offset);

	if (HAL_UART_Transmit(&huart1, (uint8_t *)(data + offset), send_len, 1000) != HAL_OK)
	    return -1;

	/* Optional small delay for ESP to process */
	HAL_Delay(5);
    }

    /* Optional: send \r\n if required by your ESP firmware */
     HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);

    /* Wait for OK response */
    return esp_wait_for("OK", 5000);
}
