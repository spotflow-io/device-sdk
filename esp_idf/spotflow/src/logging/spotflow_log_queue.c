
#include "logging/spotflow_log_queue.h"

mqd_t write_descr;
/**
 * @brief To Add a message in Queue
 * 
 * @param msg Log Message 
 */
void queue_push(const char *msg, size_t len)
{
    if (mq_send(write_descr, msg, len, 0) < 0) {
        if (errno == EAGAIN) {
            // queue full — remove oldest message
            char dropped[CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN];
            ssize_t len = mq_receive(write_descr, dropped, sizeof(dropped), NULL);
            if (len >= 0) {
                dropped[len] = '\0';
                SPOTFLOW_LOG( "Queue full — dropped oldest: %s \n", dropped);

                // retry send after dropping
                if (mq_send(write_descr, msg, len, 0) == -1) {
                    SPOTFLOW_LOG( "mq_send failed after drop: errno=%d\n", errno);
                } else {
                    SPOTFLOW_LOG( "Added new message after drop: %s\n", msg);
                }
            } else {
                SPOTFLOW_LOG( "mq_receive failed: errno=%d\n", errno);
            }
        } else {
            SPOTFLOW_LOG( "mq_send failed: errno=%d\n", errno);
        }
    } else {
        SPOTFLOW_LOG( "Added: %02X \n", msg[strlen(msg)]);
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
    bytes_read = mq_receive(write_descr, buffer, 1024, NULL);
    if (bytes_read >= 0) {
        // buffer[bytes_read] = '\0';
        // printf("Message: %s \n", buffer);
        //  SPOTFLOW_LOG("%02X ", buffer[bytes_read]);
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


