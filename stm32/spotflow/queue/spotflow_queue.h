#ifndef SPOTFLOW_QUEUE_H
#define SPOTFLOW_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "spotflow.h"

// Log message structure
typedef struct {
    uint8_t *ptr;
    size_t len;
} queue_msg_t;

/**
 * @brief Initialize the Queue to save messages
 */
void spotflow_queue_init(void);

/**
 * @brief Add a message to the queue
 * 
 * @param msg Pointer to the message buffer
 * @param len Length of the message
 */
void spotflow_queue_push(uint8_t* msg, size_t len);

/**
 * @brief Read the next message from the queue (non-blocking)
 * 
 * @param out Pointer to a queue_msg_t struct to receive data
 * @return true if a message was read, false if queue empty
 */
bool spotflow_queue_read(queue_msg_t* out);

/**
 * @brief Free the memory associated with a queue message
 * 
 * @param msg Pointer to the queue_msg_t to free
 */
void spotflow_queue_free(queue_msg_t* msg);

#ifdef __cplusplus
}
#endif

#endif // SPOTFLOW_QUEUE_H
