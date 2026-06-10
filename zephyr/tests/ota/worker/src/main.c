#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(spotflow_ota);

#include <spotflow/ota.h>

#include "ota/spotflow_ota_net.h"
#include "ota/spotflow_ota_persistence.h"
#include "ota/spotflow_ota_state.h"
#include "ota/spotflow_ota_worker.h"

#include "spotflow_ota_test_fakes.h"
#include "spotflow_ota_test_settings.h"
#include "spotflow_ota_test_wait.h"

static uint8_t published_payload[128];

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
		snprintk(artifact->version, sizeof(artifact->version), "%llu.0.%zu", attempt_id,
			 i);
	}

	return msg;
}

static void wake_worker_from_action(const struct spotflow_ota_state_action* action)
{
	if (action != NULL && action->wake_worker) {
		spotflow_ota_worker_wake();
	}
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
	k_sem_give(&fake_callbacks->handle_called_sem);

	if (fake_callbacks->block_handle) {
		k_sem_take(&fake_callbacks->handle_continue_sem, K_FOREVER);
	}

	return fake_callbacks->next_handle_result;
}

void spotflow_on_update_canceled(void)
{
}

bool spotflow_is_update_canceled(void)
{
	return spotflow_ota_state_is_update_canceled();
}

int spotflow_mqtt_publish_ota_cbor_msg(uint8_t* payload, size_t len)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();

	if (len > sizeof(published_payload)) {
		return -ENOMEM;
	}

	fake_mqtt->publish_count++;
	fake_mqtt->last_payload = published_payload;
	fake_mqtt->last_payload_len = len;
	memcpy(published_payload, payload, len);
	return fake_mqtt->publish_result;
}

ZTEST(spotflow_ota_worker, test_superseded_attempt_promoted_after_terminal_via_worker)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
	    spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg first = make_delegated_update(9, 2);
	struct spotflow_ota_update_msg second = make_delegated_update(10, 1);
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	const enum spotflow_ota_result superseded_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
		SPOTFLOW_OTA_RESULT_CANCELED,
	};
	const struct spotflow_ota_cbor_update_results superseded_message = {
		.attempt_id = 9,
		.succeeded_count = 1,
		.succeeded = { 0 },
		.canceled_count = 1,
		.canceled = { 1 },
	};

	fake_callbacks->block_handle = true;
	fake_callbacks->next_handle_result = SPOTFLOW_OTA_RESULT_SUCCEEDED;

	zassert_ok(spotflow_ota_state_accept_update(&first, &action));
	wake_worker_from_action(&action);
	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	zassert_equal(fake_callbacks->last_attempt_id, 9);
	zassert_equal(fake_callbacks->handle_call_count, 1);

	zassert_ok(spotflow_ota_state_accept_update(&second, &action));
	wake_worker_from_action(&action);

	k_sem_give(&fake_callbacks->handle_continue_sem);
	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	zassert_equal(fake_callbacks->last_attempt_id, 10);
	zassert_equal(fake_callbacks->handle_call_count, 2);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.current_attempt_id, 10);
	zassert_false(snapshot.has_pending_attempt);

	spotflow_ota_test_wait_for_persisted_attempt(9, superseded_results,
						     ARRAY_SIZE(superseded_results));
	zassert_ok(spotflow_ota_net_send_pending_message());
	spotflow_ota_test_expect_update_results_payload(&superseded_message);

	k_sem_give(&fake_callbacks->handle_continue_sem);
	spotflow_ota_test_wait_for_persisted_attempt(
	    10, (const enum spotflow_ota_result[]){ SPOTFLOW_OTA_RESULT_SUCCEEDED }, 1);
}

ZTEST(spotflow_ota_worker, test_deferred_rejection_persisted_and_reported_by_worker)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
	    spotflow_ota_test_fake_callbacks_get();
	struct spotflow_ota_update_msg first = make_delegated_update(9, 2);
	struct spotflow_ota_state_action action;
	const struct spotflow_ota_cbor_update_results rejected_message = {
		.attempt_id = 10,
		.has_attempt_error = true,
		.attempt_error = SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE,
	};

	fake_callbacks->block_handle = true;
	fake_callbacks->next_handle_result = SPOTFLOW_OTA_RESULT_SUCCEEDED;

	zassert_ok(spotflow_ota_state_accept_update(&first, &action));
	wake_worker_from_action(&action);
	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));

	zassert_ok(spotflow_ota_state_reject_update(
	    10, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE, &action));
	wake_worker_from_action(&action);

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
	struct spotflow_ota_state_action action;
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
	};
	const struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 1,
		.succeeded_count = 2,
		.succeeded = { 0, 1 },
	};

	zassert_ok(spotflow_ota_state_accept_update(&update, &action));
	wake_worker_from_action(&action);

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

ZTEST_SUITE(spotflow_ota_worker, NULL, NULL, before_each, NULL, NULL);
