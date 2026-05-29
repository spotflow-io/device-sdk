#ifndef SPOTFLOW_OTA_STATE_H
#define SPOTFLOW_OTA_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota/spotflow_ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum spotflow_ota_worker_job_type {
	SPOTFLOW_OTA_WORKER_JOB_NONE,
	SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT,
	SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT,
};

struct spotflow_ota_worker_job {
	enum spotflow_ota_worker_job_type type;
	uint64_t attempt_id;
	size_t artifact_index;
	struct spotflow_ota_artifact artifact;
	enum spotflow_ota_attempt_error attempt_error;
};

struct spotflow_ota_state_action {
	bool wake_worker;
	bool accepted_update;
	bool ignored_duplicate_update;
	bool accepted_cancel;
	bool ignored_late_cancel;
	bool rejected_attempt;
	bool report_requested;
	bool superseded_current;
	bool can_promote_pending;
	bool promoted_pending;
	uint64_t attempt_id;
};

struct spotflow_ota_state_snapshot {
	bool has_current_attempt;
	uint64_t current_attempt_id;
	size_t artifact_count;
	size_t current_artifact_index;
	bool actionable_cancellation;
	bool has_pending_attempt;
	uint64_t pending_attempt_id;
	bool has_attempt_error;
	enum spotflow_ota_attempt_error attempt_error;
	enum spotflow_ota_result artifact_results[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
	struct spotflow_ota_main_firmware_state main_firmware_state;
};

void spotflow_ota_state_reset(void);

int spotflow_ota_state_accept_update(const struct spotflow_ota_update_msg* msg,
				     struct spotflow_ota_state_action* action);

int spotflow_ota_state_reject_update(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				     struct spotflow_ota_state_action* action);

int spotflow_ota_state_accept_cancel(uint64_t attempt_id, struct spotflow_ota_state_action* action);

int spotflow_ota_state_accept_report_request(uint64_t attempt_id,
					     struct spotflow_ota_state_action* action);

bool spotflow_ota_state_get_worker_job(struct spotflow_ota_worker_job* job);

int spotflow_ota_state_apply_artifact_result(size_t artifact_index, enum spotflow_ota_result result,
					     struct spotflow_ota_state_action* action);

int spotflow_ota_state_promote_pending(struct spotflow_ota_state_action* action);

bool spotflow_ota_state_is_update_canceled(void);

void spotflow_ota_state_get_snapshot(struct spotflow_ota_state_snapshot* snapshot);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_STATE_H */
