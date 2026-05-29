#include <string.h>

#include <zephyr/ztest.h>

#include "ota/spotflow_ota_state.h"

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);

	spotflow_ota_state_reset();
}

static struct spotflow_ota_update_msg make_update(uint64_t attempt_id, size_t artifact_count)
{
	struct spotflow_ota_update_msg msg = {
		.attempt_id = attempt_id,
		.artifact_count = artifact_count,
	};

	for (size_t i = 0; i < artifact_count; i++) {
		struct spotflow_ota_artifact* artifact = &msg.artifacts[i];

		artifact->is_main = (i == 0);
		snprintk(artifact->slug, sizeof(artifact->slug), "artifact-%zu", i);
		snprintk(artifact->url, sizeof(artifact->url), "https://example/%llu/%zu",
			 attempt_id, i);
		snprintk(artifact->secret, sizeof(artifact->secret), "secret-%zu", i);
		snprintk(artifact->version, sizeof(artifact->version), "1.0.%zu", i);
	}

	return msg;
}

ZTEST(spotflow_ota_state, test_accept_first_attempt_and_ignore_duplicate)
{
	struct spotflow_ota_update_msg msg = make_update(1, 2);
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	int rc = spotflow_ota_state_accept_update(&msg, &action);

	zassert_ok(rc);
	zassert_true(action.accepted_update);
	zassert_true(action.wake_worker);
	zassert_equal(action.attempt_id, 1);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_true(snapshot.has_current_attempt);
	zassert_equal(snapshot.current_attempt_id, 1);
	zassert_equal(snapshot.artifact_count, 2);
	zassert_equal(snapshot.current_artifact_index, 0);
	zassert_false(snapshot.has_pending_attempt);

	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.type, SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT);
	zassert_equal(job.attempt_id, 1);
	zassert_equal(job.artifact_index, 0);
	zassert_str_equal(job.artifact.slug, "artifact-0");
	zassert_equal_ptr(job.artifact.download_request.url, job.artifact.url);
	zassert_equal_ptr(job.artifact.download_request.secret, job.artifact.secret);

	rc = spotflow_ota_state_accept_update(&msg, &action);

	zassert_ok(rc);
	zassert_true(action.ignored_duplicate_update);
	zassert_false(action.wake_worker);
}

ZTEST(spotflow_ota_state, test_reject_first_attempt_with_attempt_error)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	int rc = spotflow_ota_state_reject_update(
	    7, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE, &action);

	zassert_ok(rc);
	zassert_true(action.rejected_attempt);
	zassert_true(action.wake_worker);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_true(snapshot.has_current_attempt);
	zassert_equal(snapshot.current_attempt_id, 7);
	zassert_true(snapshot.has_attempt_error);
	zassert_equal(snapshot.attempt_error, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);

	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.type, SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT);
	zassert_equal(job.attempt_id, 7);
	zassert_equal(job.attempt_error, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_accept_cancel_before_success)
{
	struct spotflow_ota_update_msg msg = make_update(2, 2);
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &action));

	int rc = spotflow_ota_state_accept_cancel(2, &action);

	zassert_ok(rc);
	zassert_true(action.accepted_cancel);
	zassert_true(action.wake_worker);
	zassert_true(spotflow_ota_state_is_update_canceled());

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_true(snapshot.actionable_cancellation);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_ignore_cancel_after_success)
{
	struct spotflow_ota_update_msg msg = make_update(3, 2);
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &action));
	zassert_ok(
	    spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_SUCCEEDED, &action));

	int rc = spotflow_ota_state_accept_cancel(3, &action);

	zassert_ok(rc);
	zassert_true(action.ignored_late_cancel);
	zassert_false(action.accepted_cancel);
	zassert_false(spotflow_ota_state_is_update_canceled());

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_false(snapshot.actionable_cancellation);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_SUCCEEDED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
}

ZTEST(spotflow_ota_state, test_running_artifact_result_is_preserved_after_cancel)
{
	struct spotflow_ota_update_msg msg = make_update(11, 2);
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &action));
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.artifact_index, 0);

	zassert_ok(spotflow_ota_state_accept_cancel(11, &action));
	zassert_true(action.accepted_cancel);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);

	zassert_ok(
	    spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_SUCCEEDED, &action));

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_SUCCEEDED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
}

ZTEST(spotflow_ota_state, test_update_artifacts_with_is_canceled_finishes_attempt)
{
	struct spotflow_ota_update_msg msg = make_update(4, 2);
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	msg.is_canceled = true;

	int rc = spotflow_ota_state_accept_update(&msg, &action);

	zassert_ok(rc);
	zassert_true(action.accepted_update);
	zassert_false(action.wake_worker);
	zassert_true(spotflow_ota_state_is_update_canceled());

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_report_request_matches_current_attempt_only)
{
	struct spotflow_ota_update_msg msg = make_update(5, 1);
	struct spotflow_ota_state_action action;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &action));

	int rc = spotflow_ota_state_accept_report_request(6, &action);

	zassert_ok(rc);
	zassert_false(action.report_requested);
	zassert_false(action.wake_worker);

	rc = spotflow_ota_state_accept_report_request(5, &action);

	zassert_ok(rc);
	zassert_true(action.report_requested);
	zassert_true(action.wake_worker);
	zassert_equal(action.attempt_id, 5);
}

ZTEST(spotflow_ota_state, test_failed_artifact_cancels_remaining_artifacts)
{
	struct spotflow_ota_update_msg msg = make_update(8, 3);
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &action));

	int rc = spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_FAILED, &action);

	zassert_ok(rc);
	zassert_false(action.wake_worker);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.artifact_results[2], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_supersede_unfinished_attempt_and_promote_pending)
{
	struct spotflow_ota_update_msg first = make_update(9, 2);
	struct spotflow_ota_update_msg second = make_update(10, 1);
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&first, &action));

	int rc = spotflow_ota_state_accept_update(&second, &action);

	zassert_ok(rc);
	zassert_true(action.superseded_current);
	zassert_true(action.can_promote_pending);
	zassert_true(spotflow_ota_state_is_update_canceled());

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.current_attempt_id, 9);
	zassert_true(snapshot.has_pending_attempt);
	zassert_equal(snapshot.pending_attempt_id, 10);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);

	rc = spotflow_ota_state_promote_pending(&action);

	zassert_ok(rc);
	zassert_true(action.promoted_pending);
	zassert_equal(action.attempt_id, 10);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.current_attempt_id, 10);
	zassert_false(snapshot.has_pending_attempt);
	zassert_false(snapshot.actionable_cancellation);
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.attempt_id, 10);
	zassert_equal(job.artifact_index, 0);
}

ZTEST_SUITE(spotflow_ota_state, NULL, NULL, before_each, NULL, NULL);
