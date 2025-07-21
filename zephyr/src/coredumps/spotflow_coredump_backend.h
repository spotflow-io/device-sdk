#ifndef SPOTFLOW_COREDUMP_BACKEND_H
#define SPOTFLOW_COREDUMP_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif
extern struct k_msgq g_spotflow_core_dumps_msgq;

void process_existing_coredump();
void trigger_queue_fill();
#ifdef __cplusplus
}
#endif

#endif //SPOTFLOW_COREDUMP_BACKEND_H
