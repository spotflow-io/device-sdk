#ifndef SPOTFLOW_OTA_STATE_H
#define SPOTFLOW_OTA_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota/persistence/spotflow_ota_records_cbor.h"
#include "ota/core/spotflow_ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum spotflow_ota_worker_job_type {
	SPOTFLOW_OTA_WORKER_JOB_NONE,
	SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT,
	SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE,
	SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT,
	SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT,
	SPOTFLOW_OTA_WORKER_JOB_FINALIZE_ATTEMPT,
};

struct spotflow_ota_operation_token {
	uint64_t attempt_id;
	uint32_t generation;
};

struct spotflow_ota_process_artifact_job {
	size_t artifact_index;
	struct spotflow_ota_artifact artifact;
};

struct spotflow_ota_complete_main_firmware_job {
	size_t artifact_index;
	struct spotflow_ota_artifact artifact;
	enum spotflow_ota_result result;
};

struct spotflow_ota_rejected_attempt_job {
	enum spotflow_ota_attempt_error error;
};

struct spotflow_ota_worker_job {
	enum spotflow_ota_worker_job_type type;
	struct spotflow_ota_operation_token token;
	union {
		struct spotflow_ota_process_artifact_job process_artifact;
		struct spotflow_ota_complete_main_firmware_job complete_main_firmware;
		struct spotflow_ota_rejected_attempt_job rejected_attempt;
	} data;
};

enum spotflow_ota_state_effect {
	SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER = 1U << 0,
	SPOTFLOW_OTA_STATE_EFFECT_NOTIFY_CUSTOM_FIRMWARE_CANCELED = 1U << 1,
	SPOTFLOW_OTA_STATE_EFFECT_CANCEL_MAIN_FIRMWARE_DOWNLOAD = 1U << 2,
};

typedef uint32_t spotflow_ota_state_effects;

enum spotflow_ota_update_disposition {
	SPOTFLOW_OTA_UPDATE_STARTED,
	SPOTFLOW_OTA_UPDATE_REHYDRATED,
	SPOTFLOW_OTA_UPDATE_DUPLICATE,
	SPOTFLOW_OTA_UPDATE_QUEUED,
};

struct spotflow_ota_update_result {
	enum spotflow_ota_update_disposition disposition;
	spotflow_ota_state_effects effects;
	uint64_t current_attempt_id;
	uint64_t pending_attempt_id;
};

enum spotflow_ota_rejection_disposition {
	SPOTFLOW_OTA_REJECTION_STARTED,
	SPOTFLOW_OTA_REJECTION_QUEUED,
};

struct spotflow_ota_rejection_result {
	enum spotflow_ota_rejection_disposition disposition;
	spotflow_ota_state_effects effects;
	uint64_t current_attempt_id;
	uint64_t pending_attempt_id;
};

enum spotflow_ota_cancel_disposition {
	SPOTFLOW_OTA_CANCEL_NOT_CURRENT,
	SPOTFLOW_OTA_CANCEL_ACCEPTED,
	SPOTFLOW_OTA_CANCEL_IGNORED_LATE,
};

struct spotflow_ota_cancel_result {
	enum spotflow_ota_cancel_disposition disposition;
	spotflow_ota_state_effects effects;
};

enum spotflow_ota_report_disposition {
	SPOTFLOW_OTA_REPORT_NOT_CURRENT,
	SPOTFLOW_OTA_REPORT_REQUESTED,
};

struct spotflow_ota_report_result {
	enum spotflow_ota_report_disposition disposition;
	spotflow_ota_state_effects effects;
};

struct spotflow_ota_state_snapshot {
	bool has_current_attempt;
	uint64_t current_attempt_id;
	uint32_t current_attempt_generation;
	bool current_attempt_terminal;
	bool current_attempt_durable;
	bool manifest_available;
	bool artifact_result_commit_pending;
	size_t artifact_count;
	size_t current_artifact_index;
	bool actionable_cancellation;
	bool has_pending_attempt;
	uint64_t pending_attempt_id;
	bool pending_is_rejection;
	enum spotflow_ota_attempt_error pending_rejection_error;
	bool has_attempt_error;
	enum spotflow_ota_attempt_error attempt_error;
	enum spotflow_ota_result artifact_results[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
	struct spotflow_ota_main_firmware_state main_firmware_state;
};

void spotflow_ota_state_reset(void);

int spotflow_ota_state_init_from_persistence(const struct spotflow_ota_persisted_attempt* attempt,
					     bool has_attempt,
					     const struct spotflow_ota_probation* probation,
					     bool has_probation);

int spotflow_ota_state_accept_update(const struct spotflow_ota_update_msg* msg,
				     struct spotflow_ota_update_result* result);

int spotflow_ota_state_reject_update(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				     struct spotflow_ota_rejection_result* result);

int spotflow_ota_state_accept_cancel(uint64_t attempt_id,
				     struct spotflow_ota_cancel_result* result);

int spotflow_ota_state_accept_report_request(uint64_t attempt_id,
					     struct spotflow_ota_report_result* result);

bool spotflow_ota_state_get_worker_job(struct spotflow_ota_worker_job* job);

int spotflow_ota_state_apply_artifact_result(size_t artifact_index, enum spotflow_ota_result result,
					     spotflow_ota_state_effects* effects);

int spotflow_ota_state_stage_artifact_result(const struct spotflow_ota_worker_job* job,
					     enum spotflow_ota_result result);

int spotflow_ota_state_commit_artifact_result(const struct spotflow_ota_worker_job* job);

int spotflow_ota_state_commit_rejected_attempt(const struct spotflow_ota_worker_job* job);

int spotflow_ota_state_commit_attempt_finalization(const struct spotflow_ota_worker_job* job);

int spotflow_ota_state_complete_report_job(const struct spotflow_ota_worker_job* job,
					   bool prepared);

int spotflow_ota_state_queue_main_firmware_result(
	uint64_t attempt_id, size_t artifact_index, enum spotflow_ota_result result,
	struct spotflow_ota_main_firmware_state* out_state, spotflow_ota_state_effects* effects);

int spotflow_ota_state_fail_worker_operation(const struct spotflow_ota_operation_token* token);

int spotflow_ota_state_promote_pending(void);

bool spotflow_ota_state_is_update_canceled(void);

void spotflow_ota_state_get_snapshot(struct spotflow_ota_state_snapshot* snapshot);

int spotflow_ota_state_set_main_firmware_phase(enum spotflow_ota_phase phase,
					       struct spotflow_ota_main_firmware_state* out_state);

int spotflow_ota_state_set_main_firmware_result(enum spotflow_ota_result result,
						struct spotflow_ota_main_firmware_state* out_state);

int spotflow_ota_state_store_main_firmware_artifact(uint64_t attempt_id, size_t artifact_index,
						    const struct spotflow_ota_artifact* artifact);

int spotflow_ota_state_get_main_firmware_info(struct spotflow_firmware_info* info,
					      struct spotflow_download_request* request_out);

int spotflow_ota_state_finish_main_firmware_prereboot(void);

int spotflow_ota_state_enter_main_firmware_unconfirmed(
	struct spotflow_ota_main_firmware_state* out_state);

int spotflow_ota_state_set_main_firmware_paused(bool paused,
						struct spotflow_ota_main_firmware_state* out_state);

int spotflow_ota_state_request_main_firmware_abort(
	struct spotflow_ota_main_firmware_state* out_state);

bool spotflow_ota_state_is_main_firmware_abort_requested(void);

int spotflow_ota_state_begin_main_firmware_upgrade_commit(void);

void spotflow_ota_state_cancel_main_firmware_upgrade_commit(void);

int spotflow_ota_state_begin_main_firmware_reboot(void);

int spotflow_ota_state_get_main_firmware_artifact_index(size_t* artifact_index);

int spotflow_ota_state_resolve_main_firmware_probation(
	const struct spotflow_ota_operation_token* token);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_STATE_H */
