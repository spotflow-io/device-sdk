#ifndef SPOTFLOW_OTA_FW_MAIN_H
#define SPOTFLOW_OTA_FW_MAIN_H

#include <stddef.h>
#include <stdint.h>

#include <spotflow/ota.h>

#include "ota/spotflow_ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum spotflow_ota_result
spotflow_ota_fw_main_process_artifact(uint64_t attempt_id, size_t artifact_index,
				      const struct spotflow_ota_artifact* artifact);

void spotflow_ota_fw_main_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_FW_MAIN_H */
