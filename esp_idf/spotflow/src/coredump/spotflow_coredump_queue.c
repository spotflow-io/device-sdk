#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "logging/spotflow_log_backend.h"
#include "coredump/spotflow_coredump_queue.h"
#include "coredump/spotflow_coredump_cbor.h"
#include "spotflow.h"

static QueueHandle_t queue_handle = NULL;

#define COREDUMPS_OVERHEAD 64

bool coredump_found = false;
/**
 * @brief To Add a message in Queue
 * 
 * @param msg Log Message 
 */
int8_t spotflow_queue_coredump_push(uint8_t* msg, size_t len)
{
	queue_msg_t qmsg;
	qmsg.ptr = malloc(len);
	qmsg.len = len;

	if (!qmsg.ptr) {
		SPOTFLOW_LOG("Heap allocation failed");
		return -1;
	}

	memcpy(qmsg.ptr, msg, len);
	// Try to enqueue
	if (xQueueSend(queue_handle, &qmsg, 0) != pdPASS) {
        free(qmsg.ptr);
        return -1; //Could not add wait for it to be freed.
	}

	SPOTFLOW_LOG("Message Added.\n");
    return 0;
}

/**
 * @brief Read next message from queue (non-blocking)
 * 
 * @param out Pointer to structure receiving ptr+len
 * @return true if a message was read, false if queue empty
 */

bool spotflow_queue_coredump_read(queue_msg_t* out)
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
void spotflow_queue_coredump_free(queue_msg_t* msg)
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
void spotflow_queue_coredump_init(void)
{
	coredump_found = true;
	queue_handle = xQueueCreate(CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE + COREDUMPS_OVERHEAD, sizeof(queue_msg_t));
	if (queue_handle == NULL) {
		SPOTFLOW_LOG("Failed to create queue");
	}
}
