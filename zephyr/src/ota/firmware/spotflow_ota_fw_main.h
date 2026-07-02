#ifndef SPOTFLOW_OTA_FW_MAIN_H
#define SPOTFLOW_OTA_FW_MAIN_H

#include <stddef.h>
#include <stdint.h>

#include <spotflow/ota.h>

#include "ota/persistence/spotflow_ota_records_cbor.h"
#include "ota/core/spotflow_ota_state.h"
#include "ota/core/spotflow_ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum spotflow_ota_result
spotflow_ota_fw_main_process_artifact(uint64_t attempt_id, size_t artifact_index,
				      const struct spotflow_ota_artifact* artifact);

int spotflow_ota_fw_main_reconcile_startup(const struct spotflow_ota_probation* probation,
					   bool has_probation,
					   struct spotflow_ota_state_action* action);

int spotflow_ota_fw_main_confirm_image(struct spotflow_ota_main_firmware_state* out_state,
				       struct spotflow_ota_state_action* action);

int spotflow_ota_fw_main_pause_update(struct spotflow_ota_main_firmware_state* out_state);

int spotflow_ota_fw_main_resume_update(struct spotflow_ota_main_firmware_state* out_state);

int spotflow_ota_fw_main_fail_update(struct spotflow_ota_main_firmware_state* out_state,
				     struct spotflow_ota_state_action* action);

void spotflow_ota_fw_main_wake_if_paused(void);

void spotflow_ota_fw_main_cancel_active_download(void);

void spotflow_ota_fw_main_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_FW_MAIN_H */
