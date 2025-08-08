#ifndef SPOTFLOW_LOG_NET_H
#define SPOTFLOW_LOG_NET_H

#ifdef __cplusplus
extern "C" {
#endif

void init_logs_polling();
int poll_and_process_enqueued_logs();

#ifdef __cplusplus
}
#endif

#endif //SPOTFLOW_LOG_NET_H
