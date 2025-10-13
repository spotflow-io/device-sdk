#ifndef SPOTFLOW_LOG_JSON_H
#define SPOTFLOW_LOG_JSON_H

#include "spotflow.h"

char* log_json(const char *fem, char* body, const char* severity, size_t *out_len, const struct message_metadata *metadata);
void log_json_send(const char *fmt, char* buffer, const char log_severity, const struct message_metadata *metadata);
#endif