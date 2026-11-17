#ifndef SPOTFLOW_PERSISTENT_BUFFER_H
#define SPOTFLOW_PERSISTENT_BUFFER_H

#include <stdint.h>

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Flash format (version SPOTFLOW_PB_FORMAT_VERSION, stored in FCB's f_version):
 *
 * Each FCB entry holds a batch of messages. Each message is:
 *   - 1 byte kind (SPOTFLOW_PB_KIND_*)
 *   - 2 bytes payload length, little endian
 *   - payload: the exact CBOR bytes sent to the transport. A session record's payload is the
 *     8-byte little-endian run ID followed by the encoded session metadata.
 *
 * Every sector that holds data of a boot contains that boot's session record before the boot's
 * first message in the sector. A reader starting at any sector start therefore knows the session
 * of every message it reads.
 */
#define SPOTFLOW_PB_FORMAT_VERSION 1

#define SPOTFLOW_PB_KIND_LOG 1
#define SPOTFLOW_PB_KIND_METRIC 2
#define SPOTFLOW_PB_KIND_SESSION 3

#define SPOTFLOW_PB_MSG_HEADER_LEN 3
#define SPOTFLOW_PB_RUN_ID_LEN 8

enum spotflow_pb_state {
	SPOTFLOW_PB_STATE_PENDING,
	SPOTFLOW_PB_STATE_READY,
	SPOTFLOW_PB_STATE_FAILED,
};

/* Opens the partition and initializes the ring. Erases the partition when it holds data in a
 * different format. Sets the state to READY or FAILED. */
int spotflow_pb_init(void);

enum spotflow_pb_state spotflow_pb_get_state(void);

/* Moves all messages queued in RAM to the ring. Returns the number of stored messages or a
 * negative errno. */
int spotflow_pb_persist(void);

/* Sends the next message from the ring, preceded by session metadata when the message belongs
 * to a different session than the last one sent on this connection.
 * Returns 1 if a message was sent, 0 if the ring is empty, -EAGAIN if the transport is not
 * ready or busy, or another negative errno on a transport error. */
int spotflow_pb_send_next(void);

/* Called when the transport starts a new session, after the current session metadata has been
 * delivered to the gateway or broker. */
void spotflow_pb_on_session_start(void);

/* Called after a message is put into a RAM queue. Wakes the persister when the queue is half
 * full. */
void spotflow_pb_on_enqueued(struct k_msgq* msgq);

/* Wakes the persister thread (implemented in spotflow_persistent_buffer_thread.c). */
void spotflow_pb_wake_persister(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_PERSISTENT_BUFFER_H */
