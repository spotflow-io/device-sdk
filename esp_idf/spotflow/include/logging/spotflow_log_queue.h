#ifndef SPOTFLOW_LOG_QUEUE_H
#define SPOTFLOW_LOG_QUEUE_H

void queue_push(const char *msg, size_t len);

int queue_read(char *buffer);

void queue_init(void);

#endif