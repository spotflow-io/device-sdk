#ifndef SPOTFLOW_LOG_JSON_H
#define SPOTFLOW_LOG_JSON_H

#include "spotflow.h"

char* log_json(char* body, const char* severity);
void log_json_send(char* buffer);
#endif