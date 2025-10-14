
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <mqueue.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_queue.h"
#include "logging/spotflow_log_cbor.h"
#include "spotflow.h"

mqd_t write_descr;
/**
 * @brief To Add a message in Queue
 * 
 * @param msg Log Message 
 */
void queue_push(const char *msg, size_t len)
{
    if (mq_send(write_descr, msg, len, 0) < 0) { // Check if their is no error while writing to the queue buffer.
        if (errno == EAGAIN) {                  // In case the Queue Buffer is full remove the oldest message
            // queue full — remove oldest message
            char dropped[CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN]; //Create a temporary array of msg size.
            ssize_t len_dropped = mq_receive(write_descr, dropped, sizeof(dropped), NULL); //Read the message to empty the queue buffer on top and drop it.
            if (len_dropped >= 0) {
                SPOTFLOW_LOG( "Queue full — dropped oldest Msg. \n"); // Drop logicS
                print_cbor_hex((const uint8_t *)dropped, len_dropped);

                // retry send after dropping
                if (mq_send(write_descr, msg, len, 0) == -1) {  // Retry adding the message on top of the queue
                    SPOTFLOW_LOG( "mq_send failed after drop: errno=%d\n", errno); // In case of error do not add this log.
                } else {
                    SPOTFLOW_LOG( "Added new message after drop.\n"); // Successfully added the log
                }
            } else {
                SPOTFLOW_LOG( "mq_receive failed: errno=%d\n", errno);  // If reading the message is failed due to some reason.
            }
        } else {
            SPOTFLOW_LOG( "mq_send failed: errno=%d\n", errno); // If the message add to queue is failed due to any reason other than Queue full.
        }
    } else {
        SPOTFLOW_LOG( "Added: ");
        print_cbor_hex((const uint8_t *)msg, len);
    }
}

/**
 * @brief To read the queue message
 * 
 * @param buffer 
 * @return int In case of error it returns -1 otherwise it returns the actual buffer_size
 */

int queue_read(char *buffer)
{
    ssize_t bytes_read;
    bytes_read = mq_receive(write_descr, buffer, CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN, NULL);
    if (bytes_read >= 0) {
        return bytes_read;
    } else {
        if (errno == EAGAIN) {
            // printf("Queue is now empty \n");
            return -1;
        } else {
            SPOTFLOW_LOG( "mq_receive failed: errno=%d", errno);
        }
    }
    return -1;
}

/**
 * @brief Initialize the Queue to save the messgaes
 * 
 */
void queue_init(void)
{
    struct mq_attr configuration = {
        .mq_flags = O_NONBLOCK, // ignored by mq_open
        .mq_maxmsg = CONFIG_SPOTFLOW_MESSAGE_QUEUE_SIZE,
        .mq_msgsize = CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN,
        .mq_curmsgs = 0 // ignored by mq_open
    };

    write_descr = mq_open("/my_queue", O_CREAT | O_WRONLY | S_IRUSR | S_IWUSR | O_NONBLOCK, 0666 , &configuration);
    
    if (write_descr == (mqd_t) -1) {
        SPOTFLOW_LOG("Creating message queue failed");
        abort();
    }
}


