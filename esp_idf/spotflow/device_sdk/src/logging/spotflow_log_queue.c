#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_queue.h"
#include "logging/spotflow_log_cbor.h"
#include "spotflow.h"

static QueueHandle_t queue_handle = NULL;
/**
 * @brief To Add a message in Queue
 * 
 * @param msg Log Message 
 */
void spotflow_queue_push(uint8_t* msg, size_t len)
{
	queue_msg_t qmsg;
	qmsg.ptr = malloc(len);
	qmsg.len = len;

	if (!qmsg.ptr) {
		SPOTFLOW_LOG("Heap allocation failed");
		return;
	}

	memcpy(qmsg.ptr, msg, len);
	// Try to enqueue
	if (xQueueSend(queue_handle, &qmsg, 0) != pdPASS) {
		// Queue full → drop oldest
		queue_msg_t dropped;
		if (xQueueReceive(queue_handle, &dropped, 0) == pdPASS) {
			SPOTFLOW_LOG("Queue full — dropped oldest message");
			free(dropped.ptr);
		}

		// Retry enqueue
		if (xQueueSend(queue_handle, &qmsg, 0) != pdPASS) {
			SPOTFLOW_LOG("Queue send failed even after drop");
			free(qmsg.ptr);
			return;
		}
	}

	SPOTFLOW_LOG("Message Added.\n");
}

/**
 * @brief Read next message from queue (non-blocking)
 * 
 * @param out Pointer to structure receiving ptr+len
 * @return true if a message was read, false if queue empty
 */

bool spotflow_queue_read(queue_msg_t* out)
{
	if (queue_handle == NULL || out == NULL) {
       return false;
    }
	
	if (xQueueReceive(queue_handle, out, 0) == pdPASS)
		return true;
	return false;
}

/**
 * @brief Free the queue message buffer
 * 
 * @param msg 
 */
void spotflow_queue_free(queue_msg_t* msg)
{
	if (msg && msg->ptr) {
		free(msg->ptr);
		msg->ptr = NULL;
		msg->len = 0;
	}
}

/**
 * @brief Initialize the Queue to save the messgaes
 * 
 */
void spotflow_queue_init(void)
{
	queue_handle = xQueueCreate(CONFIG_SPOTFLOW_MESSAGE_QUEUE_SIZE, sizeof(queue_msg_t));
	if (queue_handle == NULL) {
		SPOTFLOW_LOG("Failed to create queue");
	}
}
