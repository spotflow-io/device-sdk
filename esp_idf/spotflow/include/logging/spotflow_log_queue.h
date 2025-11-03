#ifndef SPOTFLOW_LOG_QUEUE_H
#define SPOTFLOW_LOG_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint8_t* ptr;
	size_t len;
} queue_msg_t;

void queue_push(uint8_t* msg, size_t len);
bool queue_read(queue_msg_t* out);
void queue_free(queue_msg_t* msg);
void queue_init(void);

#ifdef __cplusplus
}
#endif

#endif