#ifndef SPOTFLOW_OTA_FW_CUSTOM_H
#define SPOTFLOW_OTA_FW_CUSTOM_H

#include <spotflow/ota.h>

#include "ota/core/spotflow_ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum spotflow_ota_result
spotflow_ota_fw_custom_process_artifact(uint64_t attempt_id,
					const struct spotflow_ota_artifact* artifact);

void spotflow_ota_fw_custom_notify_canceled(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_FW_CUSTOM_H */
