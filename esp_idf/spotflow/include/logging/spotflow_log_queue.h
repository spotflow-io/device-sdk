#ifndef SPOTFLOW_LOG_QUEUE_H
#define SPOTFLOW_LOG_QUEUE_H

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

void queue_push(const char *msg, size_t len);

int queue_read(char *buffer);

void queue_init(void);

#endif