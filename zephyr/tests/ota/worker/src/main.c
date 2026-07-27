#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(spotflow_ota);

#include <spotflow/ota.h>

#include "ota/protocol/spotflow_ota_net.h"
#include "ota/persistence/spotflow_ota_persistence.h"
#include "ota/core/spotflow_ota_state.h"
#include "ota/core/spotflow_ota_worker.h"

#include "spotflow_ota_test_fakes.h"
#include "spotflow_ota_test_settings.h"
#include "spotflow_ota_test_wait.h"

#define ATTEMPT_SETTINGS_PATH "spotflow/ota/attempt"

static bool fail_attempt_persistence_after_first_callback;
static bool inject_new_attempt_after_result;
static int injected_attempt_rc;
static struct spotflow_ota_update_msg injected_update;

static struct spotflow_ota_update_msg make_delegated_update(uint64_t attempt_id,
							    size_t artifact_count)
{
	struct spotflow_ota_update_msg msg = {
		.attempt_id = attempt_id,
		.artifact_count = artifact_count,
	};

	for (size_t i = 0; i < artifact_count; i++) {
		struct spotflow_ota_artifact* artifact = &msg.artifacts[i];

		artifact->is_main = false;
		snprintk(artifact->slug, sizeof(artifact->slug), "a%llu-%zu", attempt_id, i);
		snprintk(artifact->url, sizeof(artifact->url), "https://example/%llu/%zu",
			 attempt_id, i);
		snprintk(artifact->secret, sizeof(artifact->secret), "secret-%zu", i);
		snprintk(artifact->version, sizeof(artifact->version), "%llu.0.%zu", attempt_id, i);
	}

	return msg;
}

static void wake_worker_from_effects(spotflow_ota_state_effects effects)
{
	if ((effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0) {
		spotflow_ota_worker_wake();
	}
}

static void queue_reconciled_main_result(enum spotflow_ota_result result)
{
	const struct spotflow_ota_persisted_attempt attempt = {
		.attempt_id = 42,
		.artifact_count = 2,
		.artifact_results = {
			SPOTFLOW_OTA_RESULT_PENDING,
			SPOTFLOW_OTA_RESULT_PENDING,
		},
	};
	const struct spotflow_ota_probation probation = {
		.attempt_id = 42,
		.artifact_index = 0,
		.slug = "main",
		.version = "1.0.0",
	};
	spotflow_ota_state_effects effects;

	zassert_ok(spotflow_ota_persistence_save_attempt(&attempt));
	zassert_ok(spotflow_ota_persistence_save_probation(&probation));
	zassert_ok(spotflow_ota_state_init_from_persistence(&attempt, true, &probation, true));
	zassert_ok(spotflow_ota_state_queue_main_firmware_result(
		attempt.attempt_id, probation.artifact_index, result, NULL, &effects));
	zassert_true((effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
}

static void wait_for_probation_to_clear(void)
{
	for (int i = 0; i < 100; i++) {
		struct spotflow_ota_probation probation;
		bool has_probation;

		zassert_ok(spotflow_ota_persistence_load_probation(&probation, &has_probation));
		if (!has_probation) {
			return;
		}

		k_sleep(K_MSEC(10));
	}

	zassert_unreachable("Timed out waiting for OTA probation to clear");
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);

	spotflow_ota_test_settings_reset();
	spotflow_ota_test_fakes_reset();
	spotflow_ota_net_reset();
	spotflow_ota_state_reset();
	spotflow_ota_worker_reset();
	zassert_ok(spotflow_ota_persistence_init());
	zassert_ok(spotflow_ota_worker_init());
	fail_attempt_persistence_after_first_callback = false;
	inject_new_attempt_after_result = false;
	injected_attempt_rc = 0;
	memset(&injected_update, 0, sizeof(injected_update));
}

void spotflow_ota_worker_test_after_artifact_result_applied(void)
{
	struct spotflow_ota_update_result result;

	if (!inject_new_attempt_after_result) {
		return;
	}

	inject_new_attempt_after_result = false;
	injected_attempt_rc = spotflow_ota_state_accept_update(&injected_update, &result);
	wake_worker_from_effects(result.effects);
}

enum spotflow_ota_result
spotflow_on_handle_firmware_update(const struct spotflow_firmware_info* info)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();

	fake_callbacks->handle_call_count++;
	fake_callbacks->handle_thread = k_current_get();
	fake_callbacks->last_attempt_id = info->attempt_id;
	fake_callbacks->last_is_main = info->is_main;
	strncpy(fake_callbacks->last_slug, info->slug, sizeof(fake_callbacks->last_slug) - 1);
	strncpy(fake_callbacks->last_version, info->version,
		sizeof(fake_callbacks->last_version) - 1);
	if (fail_attempt_persistence_after_first_callback &&
	    fake_callbacks->handle_call_count == 1) {
		spotflow_ota_test_settings_set_save_failure(ATTEMPT_SETTINGS_PATH);
	}
	k_sem_give(&fake_callbacks->handle_called_sem);

	if (fake_callbacks->block_handle) {
		k_sem_take(&fake_callbacks->handle_continue_sem, K_FOREVER);
	}

	return fake_callbacks->next_handle_result;
}

void spotflow_on_update_canceled(void) {}

bool spotflow_is_update_canceled(void)
{
	return spotflow_ota_state_is_update_canceled();
}

ZTEST(spotflow_ota_worker, test_superseded_attempt_promoted_after_terminal_via_worker)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg first = make_delegated_update(9, 2);
	struct spotflow_ota_update_msg second = make_delegated_update(10, 1);
	struct spotflow_ota_update_result result;
	struct spotflow_ota_state_snapshot snapshot;
	const enum spotflow_ota_result superseded_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
		SPOTFLOW_OTA_RESULT_CANCELED,
	};
	const struct spotflow_ota_cbor_update_results promoted_message = {
		.attempt_id = 10,
		.succeeded_count = 1,
		.succeeded = { 0 },
	};
	struct spotflow_ota_test_fake_transport* fake_transport =
		spotflow_ota_test_fake_transport_get();

	fake_callbacks->block_handle = true;
	fake_callbacks->next_handle_result = SPOTFLOW_OTA_RESULT_SUCCEEDED;

	zassert_ok(spotflow_ota_state_accept_update(&first, &result));
	wake_worker_from_effects(result.effects);
	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	zassert_equal(fake_callbacks->last_attempt_id, 9);
	zassert_equal(fake_callbacks->handle_call_count, 1);

	zassert_ok(spotflow_ota_state_accept_update(&second, &result));
	wake_worker_from_effects(result.effects);

	k_sem_give(&fake_callbacks->handle_continue_sem);
	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	zassert_equal(fake_callbacks->last_attempt_id, 10);
	zassert_equal(fake_callbacks->handle_call_count, 2);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.current_attempt_id, 10);
	zassert_false(snapshot.has_pending_attempt);

	zassert_true(spotflow_ota_test_settings_attempt_was_saved(9, superseded_results,
								  ARRAY_SIZE(superseded_results)));
	zassert_equal(fake_transport->publish_count, 0);
	zassert_ok(spotflow_ota_net_send_pending_message());
	zassert_equal(fake_transport->publish_count, 0);

	k_sem_give(&fake_callbacks->handle_continue_sem);
	spotflow_ota_test_wait_for_persisted_attempt(
		10, (const enum spotflow_ota_result[]){ SPOTFLOW_OTA_RESULT_SUCCEEDED }, 1);
	zassert_ok(spotflow_ota_net_send_pending_message());
	spotflow_ota_test_expect_update_results_payload(&promoted_message);
}

ZTEST(spotflow_ota_worker, test_deferred_rejection_persisted_and_reported_by_worker)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg first = make_delegated_update(9, 2);
	struct spotflow_ota_update_result result;
	struct spotflow_ota_rejection_result rejection_result;
	const struct spotflow_ota_cbor_update_results rejected_message = {
		.attempt_id = 10,
		.has_attempt_error = true,
		.attempt_error = SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE,
	};

	fake_callbacks->block_handle = true;
	fake_callbacks->next_handle_result = SPOTFLOW_OTA_RESULT_SUCCEEDED;

	zassert_ok(spotflow_ota_state_accept_update(&first, &result));
	wake_worker_from_effects(result.effects);
	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));

	zassert_ok(spotflow_ota_state_reject_update(
		10, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE, &rejection_result));
	wake_worker_from_effects(rejection_result.effects);

	k_sem_give(&fake_callbacks->handle_continue_sem);
	spotflow_ota_test_wait_for_persisted_attempt_error(
		10, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
	zassert_equal(fake_callbacks->handle_call_count, 1);

	zassert_ok(spotflow_ota_net_send_pending_message());
	spotflow_ota_test_expect_update_results_payload(&rejected_message);
}

ZTEST(spotflow_ota_worker, test_multi_artifact_success_processed_in_order)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg update = make_delegated_update(1, 2);
	struct spotflow_ota_update_result result;
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
	};
	const struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 1,
		.succeeded_count = 2,
		.succeeded = { 0, 1 },
	};

	zassert_ok(spotflow_ota_state_accept_update(&update, &result));
	wake_worker_from_effects(result.effects);

	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	zassert_equal(strcmp(fake_callbacks->last_slug, "a1-0"), 0);

	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	zassert_equal(strcmp(fake_callbacks->last_slug, "a1-1"), 0);
	zassert_equal(fake_callbacks->handle_call_count, 2);

	spotflow_ota_test_wait_for_persisted_attempt(1, expected_results,
						     ARRAY_SIZE(expected_results));
	zassert_ok(spotflow_ota_net_send_pending_message());
	spotflow_ota_test_expect_update_results_payload(&expected_message);
}

ZTEST(spotflow_ota_worker, test_accepted_attempt_persisted_before_artifact_processing)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg update = make_delegated_update(1, 1);
	struct spotflow_ota_update_result result;
	struct spotflow_ota_persisted_attempt attempt;
	const enum spotflow_ota_result expected_pending_results[] = {
		SPOTFLOW_OTA_RESULT_PENDING,
	};
	bool has_attempt;

	fake_callbacks->block_handle = true;
	fake_callbacks->next_handle_result = SPOTFLOW_OTA_RESULT_SUCCEEDED;

	zassert_ok(spotflow_ota_state_accept_update(&update, &result));
	wake_worker_from_effects(result.effects);
	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));

	zassert_ok(spotflow_ota_persistence_load_attempt(&attempt, &has_attempt));
	zassert_true(has_attempt);
	zassert_equal(attempt.attempt_id, 1);
	zassert_equal(attempt.artifact_count, 1);
	zassert_false(attempt.has_attempt_error);
	zassert_mem_equal(attempt.artifact_results, expected_pending_results,
			  sizeof(expected_pending_results));

	k_sem_give(&fake_callbacks->handle_continue_sem);
	k_msleep(50);
}

ZTEST(spotflow_ota_worker, test_initial_attempt_persistence_failure_is_retried)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg update = make_delegated_update(1, 1);
	struct spotflow_ota_update_result result;
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
	};

	spotflow_ota_test_settings_set_save_failure_once(ATTEMPT_SETTINGS_PATH);
	zassert_ok(spotflow_ota_state_accept_update(&update, &result));
	wake_worker_from_effects(result.effects);

	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)),
		   "worker did not retry the pre-handler attempt save");
	spotflow_ota_test_wait_for_persisted_attempt(1, expected_results,
						     ARRAY_SIZE(expected_results));
	zassert_equal(fake_callbacks->handle_call_count, 1);
}

ZTEST(spotflow_ota_worker, test_installed_version_load_failure_is_retried)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg update = make_delegated_update(1, 1);
	struct spotflow_ota_update_result result;
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
	};

	spotflow_ota_test_settings_set_load_failure_once(-EIO);
	zassert_ok(spotflow_ota_state_accept_update(&update, &result));
	wake_worker_from_effects(result.effects);

	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)),
		   "worker did not retry the installed-version load");
	spotflow_ota_test_wait_for_persisted_attempt(1, expected_results,
						     ARRAY_SIZE(expected_results));
	zassert_equal(fake_callbacks->handle_call_count, 1);
}

ZTEST(spotflow_ota_worker, test_installed_version_save_failure_is_retried_without_handler)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg update = make_delegated_update(1, 1);
	struct spotflow_ota_update_result result;
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
	};

	spotflow_ota_test_settings_set_save_failure_once("spotflow/ota/version/a1-0");
	zassert_ok(spotflow_ota_state_accept_update(&update, &result));
	wake_worker_from_effects(result.effects);

	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	spotflow_ota_test_wait_for_persisted_attempt(1, expected_results,
						     ARRAY_SIZE(expected_results));
	zassert_equal(fake_callbacks->handle_call_count, 1,
		      "persistence retry must not execute the physical handler twice");
}

ZTEST(spotflow_ota_worker, test_rejected_attempt_persistence_failure_is_retried)
{
	struct spotflow_ota_rejection_result result;

	spotflow_ota_test_settings_set_save_failure_once(ATTEMPT_SETTINGS_PATH);
	zassert_ok(spotflow_ota_state_reject_update(
		1, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE, &result));
	wake_worker_from_effects(result.effects);

	spotflow_ota_test_wait_for_persisted_attempt_error(
		1, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
}

ZTEST(spotflow_ota_worker, test_next_artifact_waits_for_previous_result_persistence)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg update = make_delegated_update(1, 2);
	struct spotflow_ota_update_result result;
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
	};

	fail_attempt_persistence_after_first_callback = true;
	zassert_ok(spotflow_ota_state_accept_update(&update, &result));
	wake_worker_from_effects(result.effects);
	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));

	zassert_equal(k_sem_take(&fake_callbacks->handle_called_sem, K_MSEC(100)), -EAGAIN,
		      "next artifact started before the previous result became durable");

	spotflow_ota_test_settings_clear_save_failure();
	spotflow_ota_worker_wake();
	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	spotflow_ota_test_wait_for_persisted_attempt(1, expected_results,
						     ARRAY_SIZE(expected_results));
	zassert_equal(fake_callbacks->handle_call_count, 2);
}

ZTEST(spotflow_ota_worker, test_terminal_result_is_persisted_before_new_attempt_replaces_it)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg first = make_delegated_update(1, 1);
	struct spotflow_ota_update_result result;
	const enum spotflow_ota_result first_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
	};

	injected_update = make_delegated_update(2, 1);
	inject_new_attempt_after_result = true;
	zassert_ok(spotflow_ota_state_accept_update(&first, &result));
	wake_worker_from_effects(result.effects);

	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	zassert_ok(injected_attempt_rc);
	zassert_true(spotflow_ota_test_settings_attempt_was_saved(1, first_results,
								  ARRAY_SIZE(first_results)),
		     "new attempt replaced the terminal result before it became durable");
}

ZTEST(spotflow_ota_worker, test_invalid_handler_result_is_reported_as_failed)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg update = make_delegated_update(1, 1);
	struct spotflow_ota_update_result result;
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_FAILED,
	};

	fake_callbacks->next_handle_result = (enum spotflow_ota_result)99;
	zassert_ok(spotflow_ota_state_accept_update(&update, &result));
	wake_worker_from_effects(result.effects);

	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	spotflow_ota_test_wait_for_persisted_attempt(1, expected_results,
						     ARRAY_SIZE(expected_results));
	zassert_equal(fake_callbacks->handle_call_count, 1);
}

ZTEST(spotflow_ota_worker, test_permanent_persistence_error_is_not_retried_forever)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg update = make_delegated_update(1, 1);
	struct spotflow_ota_update_result result;
	struct spotflow_ota_state_snapshot snapshot;

	spotflow_ota_test_settings_set_save_failure_error(ATTEMPT_SETTINGS_PATH, -EINVAL);
	zassert_ok(spotflow_ota_state_accept_update(&update, &result));
	wake_worker_from_effects(result.effects);
	k_msleep(100);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_true(snapshot.has_attempt_error);
	zassert_equal(snapshot.attempt_error, SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ERROR);
	zassert_equal(fake_callbacks->handle_call_count, 0);
	zassert_equal(spotflow_ota_test_settings_get_save_failure_count(), 2);

	k_msleep(100);
	zassert_equal(spotflow_ota_test_settings_get_save_failure_count(), 2,
		      "deterministic validation failures must not schedule retries");
}

ZTEST(spotflow_ota_worker, test_terminal_finalization_retries_storage_failure)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
		spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg update = make_delegated_update(1, 1);
	struct spotflow_ota_update_result result;
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_CANCELED,
	};
	const struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 1,
		.canceled_count = 1,
		.canceled = { 0 },
	};

	update.is_canceled = true;
	spotflow_ota_test_settings_set_save_failure_once(ATTEMPT_SETTINGS_PATH);
	zassert_ok(spotflow_ota_state_accept_update(&update, &result));
	wake_worker_from_effects(result.effects);

	spotflow_ota_test_wait_for_persisted_attempt(1, expected_results,
						     ARRAY_SIZE(expected_results));
	zassert_equal(fake_callbacks->handle_call_count, 0);
	zassert_ok(spotflow_ota_net_send_pending_message());
	spotflow_ota_test_expect_update_results_payload(&expected_message);
}

ZTEST(spotflow_ota_worker, test_durable_terminal_attempt_is_not_finalized_again)
{
	const struct spotflow_ota_persisted_attempt persisted = {
		.attempt_id = 1,
		.artifact_count = 1,
		.artifact_results = { SPOTFLOW_OTA_RESULT_SUCCEEDED },
	};

	zassert_ok(spotflow_ota_persistence_save_attempt(&persisted));
	zassert_ok(spotflow_ota_state_init_from_persistence(&persisted, true, NULL, false));
	spotflow_ota_test_settings_set_save_failure(ATTEMPT_SETTINGS_PATH);

	spotflow_ota_worker_wake();
	k_sleep(K_MSEC(100));

	zassert_equal(spotflow_ota_test_settings_get_save_failure_count(), 0,
		      "a durable terminal attempt must not be finalized after a spurious wake");
}

ZTEST(spotflow_ota_worker, test_reconciled_main_success_uses_durable_artifact_pipeline)
{
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
		SPOTFLOW_OTA_RESULT_PENDING,
	};
	const struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 42,
		.succeeded_count = 1,
		.succeeded = { 0 },
	};
	struct spotflow_ota_persisted_attempt persisted_attempt;
	struct spotflow_ota_probation probation;
	char installed_version[SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1];
	bool has_attempt;
	bool has_probation;
	bool has_version;

	queue_reconciled_main_result(SPOTFLOW_OTA_RESULT_SUCCEEDED);
	spotflow_ota_test_settings_set_save_failure(ATTEMPT_SETTINGS_PATH);
	spotflow_ota_worker_wake();
	k_sleep(K_MSEC(100));

	zassert_ok(spotflow_ota_persistence_load_attempt(&persisted_attempt, &has_attempt));
	zassert_true(has_attempt);
	zassert_equal(persisted_attempt.artifact_results[0], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_ok(spotflow_ota_persistence_load_probation(&probation, &has_probation));
	zassert_true(has_probation, "probation must remain until the result is durable");

	spotflow_ota_test_settings_clear_save_failure();
	spotflow_ota_worker_wake();
	spotflow_ota_test_wait_for_persisted_attempt(42, expected_results,
						     ARRAY_SIZE(expected_results));
	wait_for_probation_to_clear();

	zassert_ok(spotflow_ota_persistence_load_installed_version(
		"main", installed_version, sizeof(installed_version), &has_version));
	zassert_true(has_version);
	zassert_str_equal(installed_version, "1.0.0");
	zassert_ok(spotflow_ota_net_send_pending_message());
	spotflow_ota_test_expect_update_results_payload(&expected_message);
}

ZTEST(spotflow_ota_worker, test_reconciled_main_rollback_uses_artifact_pipeline)
{
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_FAILED,
		SPOTFLOW_OTA_RESULT_CANCELED,
	};
	const struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 42,
		.failed_count = 1,
		.failed = { 0 },
		.canceled_count = 1,
		.canceled = { 1 },
	};
	char installed_version[SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1];
	bool has_version;

	queue_reconciled_main_result(SPOTFLOW_OTA_RESULT_FAILED);
	spotflow_ota_worker_wake();
	spotflow_ota_test_wait_for_persisted_attempt(42, expected_results,
						     ARRAY_SIZE(expected_results));
	wait_for_probation_to_clear();

	zassert_ok(spotflow_ota_persistence_load_installed_version(
		"main", installed_version, sizeof(installed_version), &has_version));
	zassert_false(has_version);
	zassert_ok(spotflow_ota_net_send_pending_message());
	spotflow_ota_test_expect_update_results_payload(&expected_message);
}

ZTEST_SUITE(spotflow_ota_worker, NULL, NULL, before_each, NULL, NULL);
