#ifndef SPOTFLOW_OTA_MAIN_MODEL_H
#define SPOTFLOW_OTA_MAIN_MODEL_H

#include "ota/core/spotflow_ota_state_model.h"

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_ota_probation;

struct ota_main_completion_transition {
	size_t artifact_index;
	struct spotflow_ota_artifact artifact;
	enum spotflow_ota_result result;
	bool claim_artifact_transaction;
};

struct ota_main_prereboot_transition {
	struct spotflow_ota_main_firmware_state state;
	bool clear_artifact_transaction;
};

void spotflow_ota_main_clear(struct ota_main_firmware_model* main);
bool spotflow_ota_main_is_valid(const struct ota_main_firmware_model* main);
void spotflow_ota_main_project_state(const struct ota_main_firmware_model* main,
				     struct spotflow_ota_main_firmware_state* out_state);
bool spotflow_ota_main_probation_is_pending(const struct ota_main_firmware_model* main);
bool spotflow_ota_main_upgrade_is_irreversible(const struct ota_main_firmware_model* main);

void spotflow_ota_main_restore_probation(struct ota_main_firmware_model* main,
					 const struct spotflow_ota_probation* probation,
					 bool result_pending);

int spotflow_ota_main_claim(struct ota_main_firmware_model* main, size_t artifact_index,
			    const struct spotflow_ota_artifact* artifact, bool handler_owned,
			    struct spotflow_ota_main_firmware_state* out_state);
int spotflow_ota_main_download_pending(struct ota_main_firmware_model* main, bool handler_owned,
				       struct spotflow_ota_main_firmware_state* out_state);
int spotflow_ota_main_download_started(struct ota_main_firmware_model* main, bool handler_owned,
				       struct spotflow_ota_main_firmware_state* out_state);
int spotflow_ota_main_download_completed(struct ota_main_firmware_model* main, bool handler_owned,
					 struct spotflow_ota_main_firmware_state* out_state);
int spotflow_ota_main_fail_handler(struct ota_main_firmware_model* main,
				   struct spotflow_ota_main_firmware_state* out_state);
void spotflow_ota_main_finish_handler(struct ota_main_firmware_model* main,
				      enum spotflow_ota_result result);
void spotflow_ota_main_fail_operation(struct ota_main_firmware_model* main);

int spotflow_ota_main_queue_reconciled_result(struct ota_main_firmware_model* main,
					      size_t artifact_index,
					      enum spotflow_ota_result result,
					      struct spotflow_ota_main_firmware_state* out_state);
bool spotflow_ota_main_claim_completion(struct ota_main_firmware_model* main,
					struct ota_main_completion_transition* transition);
int spotflow_ota_main_commit_probation_cleared(struct ota_main_firmware_model* main,
					       bool reconciliation_owned);

int spotflow_ota_main_finish_prereboot(struct ota_main_firmware_model* main, bool handler_owned,
				       struct ota_main_prereboot_transition* transition);
int spotflow_ota_main_enter_unconfirmed(struct ota_main_firmware_model* main, size_t artifact_index,
					struct spotflow_ota_main_firmware_state* out_state);
int spotflow_ota_main_set_paused(struct ota_main_firmware_model* main, bool has_attempt,
				 bool paused, struct spotflow_ota_main_firmware_state* out_state);
int spotflow_ota_main_request_abort(struct ota_main_firmware_model* main, bool has_attempt,
				    struct spotflow_ota_main_firmware_state* out_state);
bool spotflow_ota_main_is_abort_requested(const struct ota_main_firmware_model* main,
					  bool has_attempt);
int spotflow_ota_main_begin_upgrade_commit(struct ota_main_firmware_model* main, bool handler_owned,
					   bool attempt_canceled);
int spotflow_ota_main_cancel_upgrade_commit(struct ota_main_firmware_model* main);
int spotflow_ota_main_begin_reboot(struct ota_main_firmware_model* main);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_MAIN_MODEL_H */
