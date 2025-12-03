#ifndef SPOTFLOW_METRICS_NET_H
#define SPOTFLOW_METRICS_NET_H

#ifdef __cplusplus
extern "C" {
#endif

void spotflow_init_metrics_polling(void);
int spotflow_poll_and_process_enqueued_metrics(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_NET_H */
