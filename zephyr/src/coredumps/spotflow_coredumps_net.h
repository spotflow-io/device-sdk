#ifndef SPOTFLOW_COREDUMPS_NET_H
#define SPOTFLOW_COREDUMPS_NET_H

#ifdef __cplusplus
extern "C" {
#endif

void spotflow_init_core_dumps_polling();
int spotflow_poll_and_process_enqueued_coredump_chunks();

#ifdef __cplusplus
}
#endif

#endif //SPOTFLOW_COREDUMPS_NET_H
