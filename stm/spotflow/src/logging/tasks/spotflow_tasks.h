#ifndef LOGGING_SPOTFLOW_TASKS_H
#define LOGGING_SPOTFLOW_TASKS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

bool spotflow_tasks_start(void);

#ifdef __cplusplus
}
#endif

#endif /* LOGGING_SPOTFLOW_TASKS_H */