#ifndef SPOTFLOW_OTA_NET_H
#define SPOTFLOW_OTA_NET_H

#include <stddef.h>
#include <stdint.h>

#include <spotflow/ota.h>

#include "ota/core/spotflow_ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void spotflow_ota_net_reset(void);

int spotflow_ota_net_prepare_results(uint64_t attempt_id,
				     const enum spotflow_ota_result* artifact_results,
				     size_t artifact_count);

int spotflow_ota_net_prepare_attempt_error(uint64_t attempt_id,
					   enum spotflow_ota_attempt_error attempt_error);

int spotflow_ota_net_send_pending_message(void);

void spotflow_ota_net_discard_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_NET_H */
