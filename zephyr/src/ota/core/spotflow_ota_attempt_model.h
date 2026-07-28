#ifndef SPOTFLOW_OTA_ATTEMPT_MODEL_H
#define SPOTFLOW_OTA_ATTEMPT_MODEL_H

#include "ota/core/spotflow_ota_state_model.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ota_attempt_constraints {
	bool main_upgrade_irreversible;
	bool probation_artifact_pending;
	size_t probation_artifact_index;
};

enum ota_attempt_cancel_outcome {
	OTA_ATTEMPT_CANCEL_NOT_CURRENT,
	OTA_ATTEMPT_CANCEL_ACCEPTED,
	OTA_ATTEMPT_CANCEL_IGNORED_LATE,
};

struct ota_attempt_cancel_transition {
	enum ota_attempt_cancel_outcome outcome;
	bool wake_worker;
};

enum ota_attempt_update_outcome {
	OTA_ATTEMPT_UPDATE_STARTED,
	OTA_ATTEMPT_UPDATE_REHYDRATED,
	OTA_ATTEMPT_UPDATE_DUPLICATE,
	OTA_ATTEMPT_UPDATE_QUEUED,
};

struct ota_attempt_update_transition {
	enum ota_attempt_update_outcome outcome;
	bool request_report;
	bool wake_worker;
	bool cancel_main_firmware;
	uint64_t current_attempt_id;
	uint64_t pending_attempt_id;
};

enum ota_attempt_rejection_outcome {
	OTA_ATTEMPT_REJECTION_STARTED,
	OTA_ATTEMPT_REJECTION_QUEUED,
};

struct ota_attempt_rejection_transition {
	enum ota_attempt_rejection_outcome outcome;
	bool wake_worker;
	bool cancel_main_firmware;
	uint64_t current_attempt_id;
	uint64_t pending_attempt_id;
};

void spotflow_ota_attempt_clear(struct ota_attempt_model* attempt);
uint32_t spotflow_ota_attempt_allocate_generation(uint32_t* next_generation);
bool spotflow_ota_attempt_exists(const struct ota_attempt_model* attempt);
bool spotflow_ota_attempt_transaction_is_active(const struct ota_attempt_model* attempt);
bool spotflow_ota_attempt_result_commit_is_pending(const struct ota_attempt_model* attempt);
bool spotflow_ota_attempt_is_durably_terminal(const struct ota_attempt_model* attempt);
bool spotflow_ota_attempt_is_valid(const struct ota_attempt_model* attempt,
				   struct ota_attempt_constraints constraints);

int spotflow_ota_attempt_validate_update(const struct spotflow_ota_update_msg* msg);
int spotflow_ota_attempt_accept_update(struct ota_attempt_model* attempt,
				       struct ota_pending_attempt* pending,
				       uint32_t* next_generation,
				       const struct spotflow_ota_update_msg* msg,
				       struct ota_attempt_constraints constraints,
				       struct ota_attempt_update_transition* transition);
void spotflow_ota_attempt_reject_update(struct ota_attempt_model* attempt,
					struct ota_pending_attempt* pending,
					uint32_t* next_generation, uint64_t attempt_id,
					enum spotflow_ota_attempt_error error,
					struct ota_attempt_constraints constraints,
					struct ota_attempt_rejection_transition* transition);
void spotflow_ota_attempt_start(const struct spotflow_ota_update_msg* msg, uint32_t generation,
				struct ota_attempt_model* attempt);
int spotflow_ota_attempt_rehydrate(const struct spotflow_ota_update_msg* msg,
				   struct ota_attempt_model* attempt,
				   struct ota_attempt_constraints constraints, bool* request_report,
				   bool* wake_worker);
void spotflow_ota_attempt_start_rejected(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
					 uint32_t generation, struct ota_attempt_model* attempt);

void spotflow_ota_pending_attempt_clear(struct ota_pending_attempt* pending);
void spotflow_ota_pending_attempt_store_update(struct ota_pending_attempt* pending,
					       const struct spotflow_ota_update_msg* msg);
void spotflow_ota_pending_attempt_store_rejection(struct ota_pending_attempt* pending,
						  uint64_t attempt_id,
						  enum spotflow_ota_attempt_error error);
uint64_t spotflow_ota_pending_attempt_id(const struct ota_pending_attempt* pending);
bool spotflow_ota_attempt_promote_pending(struct ota_attempt_model* attempt,
					  struct ota_pending_attempt* pending, uint32_t generation);

struct ota_attempt_cancel_transition
spotflow_ota_attempt_accept_cancel(struct ota_attempt_model* attempt, uint64_t attempt_id,
				   struct ota_attempt_constraints constraints);
bool spotflow_ota_attempt_supersede(struct ota_attempt_model* attempt,
				    struct ota_attempt_constraints constraints);

bool spotflow_ota_attempt_has_terminal_results(const struct ota_attempt_model* attempt);
bool spotflow_ota_attempt_has_projected_terminal_results(
	const struct ota_attempt_model* attempt, struct ota_attempt_constraints constraints);
bool spotflow_ota_attempt_has_reportable_results(const struct ota_attempt_model* attempt);
bool spotflow_ota_attempt_has_succeeded_artifact(const struct ota_attempt_model* attempt);
bool spotflow_ota_attempt_has_runnable_artifact(const struct ota_attempt_model* attempt,
						struct ota_attempt_constraints constraints);

void spotflow_ota_attempt_stage_result(struct ota_attempt_model* attempt, size_t artifact_index,
				       enum spotflow_ota_result result,
				       enum ota_artifact_result_source source);
void spotflow_ota_attempt_apply_staged_result(struct ota_attempt_model* attempt,
					      struct ota_attempt_constraints constraints);
void spotflow_ota_attempt_project_results(const struct ota_attempt_model* attempt,
					  struct ota_attempt_constraints constraints,
					  enum spotflow_ota_result* results);
void spotflow_ota_attempt_cancel_pending_artifacts(struct ota_attempt_model* attempt,
						   struct ota_attempt_constraints constraints);
void spotflow_ota_attempt_advance(struct ota_attempt_model* attempt);
void spotflow_ota_attempt_refresh_lifecycle(struct ota_attempt_model* attempt,
					    struct ota_attempt_constraints constraints);
void spotflow_ota_attempt_clear_transaction(struct ota_attempt_model* attempt);
void spotflow_ota_attempt_advance_mutation_revision(struct ota_attempt_model* attempt);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_ATTEMPT_MODEL_H */
