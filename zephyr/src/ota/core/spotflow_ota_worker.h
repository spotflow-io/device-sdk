#ifndef SPOTFLOW_OTA_WORKER_H
#define SPOTFLOW_OTA_WORKER_H

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_ota_worker_init(void);

void spotflow_ota_worker_wake(void);

void spotflow_ota_worker_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_WORKER_H */
