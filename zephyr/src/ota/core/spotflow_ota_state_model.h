#ifndef SPOTFLOW_OTA_STATE_MODEL_H
#define SPOTFLOW_OTA_STATE_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota/core/spotflow_ota_types.h"

enum ota_attempt_lifecycle {
	OTA_ATTEMPT_EMPTY,
	OTA_ATTEMPT_AWAITING_MANIFEST,
	OTA_ATTEMPT_ACTIVE,
	OTA_ATTEMPT_FINALIZING,
	OTA_ATTEMPT_FINALIZATION_CLAIMED,
	OTA_ATTEMPT_TERMINAL,
	OTA_ATTEMPT_REJECTING,
	OTA_ATTEMPT_REJECTION_CLAIMED,
	OTA_ATTEMPT_REJECTED,
};

struct ota_attempt_identity {
	enum ota_attempt_lifecycle lifecycle;
	uint64_t id;
	uint32_t generation;
};

enum ota_artifact_plan_source {
	OTA_ARTIFACT_PLAN_NONE,
	OTA_ARTIFACT_PLAN_PROBATION_PREFIX,
	OTA_ARTIFACT_PLAN_PERSISTED_RESULTS,
	OTA_ARTIFACT_PLAN_FULL_MANIFEST,
};

enum ota_artifact_transaction_state {
	OTA_ARTIFACT_TRANSACTION_IDLE,
	OTA_ARTIFACT_TRANSACTION_RUNNING,
	OTA_ARTIFACT_TRANSACTION_RESULT_STAGED,
};

enum ota_artifact_result_source {
	OTA_ARTIFACT_RESULT_HANDLER,
	OTA_ARTIFACT_RESULT_MAIN_RECONCILIATION,
};

struct ota_artifact_result_mutation {
	size_t artifact_index;
	enum spotflow_ota_result result;
	bool cancel_remaining;
	enum ota_artifact_result_source source;
};

struct ota_artifact_transaction {
	enum ota_artifact_transaction_state state;
	size_t artifact_index;
	uint32_t mutation_revision;
	struct ota_artifact_result_mutation mutation;
};

struct ota_artifact_plan {
	enum ota_artifact_plan_source source;
	size_t count;
	struct spotflow_ota_artifact artifacts[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
	enum spotflow_ota_result results[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
};

enum ota_artifact_sequence_policy {
	OTA_ARTIFACT_SEQUENCE_CONTINUE,
	OTA_ARTIFACT_SEQUENCE_STOP_REMAINING,
};

struct ota_artifact_execution {
	size_t next_index;
	struct ota_artifact_transaction transaction;
	bool cancellation_requested;
	enum ota_artifact_sequence_policy sequence_policy;
};

enum ota_report_state {
	OTA_REPORT_IDLE,
	OTA_REPORT_REQUESTED,
	OTA_REPORT_CLAIMED,
	OTA_REPORT_CLAIMED_RERUN_REQUESTED,
	OTA_REPORT_BLOCKED,
};

enum ota_main_execution_state {
	OTA_MAIN_EXECUTION_IDLE,
	OTA_MAIN_EXECUTION_CLAIMED,
	OTA_MAIN_EXECUTION_PENDING_DOWNLOAD,
	OTA_MAIN_EXECUTION_DOWNLOADING,
	OTA_MAIN_EXECUTION_PENDING_UPGRADE,
	OTA_MAIN_EXECUTION_COMMITTING,
	OTA_MAIN_EXECUTION_REBOOT_READY,
	OTA_MAIN_EXECUTION_REBOOT_STARTED,
	OTA_MAIN_EXECUTION_UNCONFIRMED,
};

enum ota_main_probation_state {
	OTA_MAIN_PROBATION_NONE,
	OTA_MAIN_PROBATION_PENDING,
	OTA_MAIN_PROBATION_COMPLETION_QUEUED,
	OTA_MAIN_PROBATION_COMPLETION_CLAIMED,
};

enum ota_pending_attempt_kind {
	OTA_PENDING_ATTEMPT_NONE,
	OTA_PENDING_ATTEMPT_UPDATE,
	OTA_PENDING_ATTEMPT_REJECTION,
};

struct ota_pending_rejection {
	uint64_t attempt_id;
	enum spotflow_ota_attempt_error error;
};

struct ota_pending_attempt {
	enum ota_pending_attempt_kind kind;
	union {
		struct spotflow_ota_update_msg update;
		struct ota_pending_rejection rejection;
	} data;
};

enum ota_attempt_failure_state {
	OTA_ATTEMPT_FAILURE_NONE,
	OTA_ATTEMPT_FAILURE_PRESENT,
};

struct ota_attempt_failure {
	enum ota_attempt_failure_state state;
	enum spotflow_ota_attempt_error error;
};

enum ota_main_firmware_presence {
	OTA_MAIN_FIRMWARE_ABSENT,
	OTA_MAIN_FIRMWARE_PRESENT,
};

struct ota_main_firmware_probation {
	enum ota_main_probation_state state;
};

struct ota_main_firmware_model {
	enum ota_main_firmware_presence presence;
	size_t artifact_index;
	struct spotflow_ota_artifact artifact;
	enum ota_main_execution_state execution;
	enum spotflow_ota_result result;
	bool is_paused;
	bool abort_requested;
	struct ota_main_firmware_probation probation;
};

struct ota_attempt_model {
	struct ota_attempt_identity identity;
	struct ota_artifact_plan plan;
	struct ota_artifact_execution execution;
	struct ota_attempt_failure failure;
};

#endif /* SPOTFLOW_OTA_STATE_MODEL_H */
