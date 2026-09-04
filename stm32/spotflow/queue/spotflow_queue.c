#include "spotflow_queue.h"
#include <stdlib.h>
#include <string.h>

// Logging macro stub
#ifndef SPOTFLOW_LOG
#define SPOTFLOW_LOG(fmt, ...) // Implement logging if needed
#endif

static spotflow_queue_t queue_handle = NULL;

/**
 * @brief Initialize the Queue to save messages
 */
void spotflow_queue_init(void)
{
    queue_handle = spotflow_port_queue_create(SPOTFLOW_MESSAGE_QUEUE_SIZE, sizeof(queue_msg_t));
    if (queue_handle == NULL) {
        // SPOTFLOW_LOG("Failed to create queue");
    }
}

/**
 * @brief Add a message to the queue
 */
void spotflow_queue_push(uint8_t* msg, size_t len)
{
    if (!msg || len == 0 || queue_handle == NULL) return;

    queue_msg_t qmsg;
    qmsg.ptr = malloc(len);
    qmsg.len = len;

    if (!qmsg.ptr) {
        // SPOTFLOW_LOG("Heap allocation failed");
        return;
    }

    memcpy(qmsg.ptr, msg, len);

    // Try to enqueue
    if (spotflow_port_queue_put(queue_handle, &qmsg) != 0) {
        // Queue full → drop oldest
        queue_msg_t dropped;
        if (spotflow_port_queue_get(queue_handle, &dropped, 0) == 0) {
            // SPOTFLOW_LOG("Queue full — dropped oldest message");
            free(dropped.ptr);
        }

        // Retry enqueue
        if (spotflow_port_queue_put(queue_handle, &qmsg) != 0) {
            // SPOTFLOW_LOG("Queue send failed even after drop");
            free(qmsg.ptr);
            return;
        }
    }

    // SPOTFLOW_LOG("Message Added.\n");
}

/**
 * @brief Read next message from queue (non-blocking)
 */
bool spotflow_queue_read(queue_msg_t* out)
{
    if (queue_handle == NULL || out == NULL) return false;

    return (spotflow_port_queue_get(queue_handle, out, 0) == 0);
}

/**
 * @brief Free the queue message buffer
 */
void spotflow_queue_free(queue_msg_t* msg)
{
    if (msg && msg->ptr) {
        free(msg->ptr);
        msg->ptr = NULL;
        msg->len = 0;
    }
}