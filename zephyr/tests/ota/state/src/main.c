#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "ota/core/spotflow_ota_state.h"

LOG_MODULE_REGISTER(spotflow_ota);

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

static struct spotflow_ota_operation_token
operation_token_from_job(const struct spotflow_ota_worker_job* job)
{
	return (struct spotflow_ota_operation_token){
		.attempt_id = job->attempt_id,
		.generation = job->generation,
	};
}

ZTEST(spotflow_ota_state, test_rejects_artifact_slug_that_cannot_be_persisted)
{
	struct spotflow_ota_update_msg msg = make_update(1, 1);
	struct spotflow_ota_update_result result;
	struct spotflow_ota_worker_job job;

	strcpy(msg.artifacts[0].slug, "invalid/slug");
	zassert_equal(spotflow_ota_state_accept_update(&msg, &result), -EINVAL);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_restored_unfinished_attempt_waits_for_manifest)
{
	const struct spotflow_ota_persisted_attempt persisted = {
		.attempt_id = 1,
		.artifact_count = 2,
		.artifact_results = {
			SPOTFLOW_OTA_RESULT_SUCCEEDED,
			SPOTFLOW_OTA_RESULT_PENDING,
		},
	};
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_init_from_persistence(&persisted, true, NULL, false));
	zassert_false(spotflow_ota_state_get_worker_job(&job),
		      "restored attempt must not run without artifact descriptors");
}

ZTEST(spotflow_ota_state, test_same_attempt_manifest_rehydrates_restored_attempt)
{
	const struct spotflow_ota_persisted_attempt persisted = {
		.attempt_id = 1,
		.artifact_count = 2,
		.artifact_results = {
			SPOTFLOW_OTA_RESULT_SUCCEEDED,
			SPOTFLOW_OTA_RESULT_PENDING,
		},
	};
	struct spotflow_ota_update_msg msg = make_update(1, 2);
	struct spotflow_ota_update_result result;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_init_from_persistence(&persisted, true, NULL, false));
	zassert_ok(spotflow_ota_state_accept_update(&msg, &result));
	zassert_equal(result.disposition, SPOTFLOW_OTA_UPDATE_REHYDRATED);
	zassert_true((result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0,
		     "rehydrated attempt must resume pending work");

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_SUCCEEDED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.type, SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT);
	zassert_equal(job.attempt_id, 1);
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.attempt_id, 1);
	zassert_equal(job.artifact_index, 1);
	zassert_str_equal(job.artifact.slug, msg.artifacts[1].slug);
	zassert_str_equal(job.artifact.url, msg.artifacts[1].url);
	zassert_str_equal(job.artifact.secret, msg.artifacts[1].secret);
	zassert_str_equal(job.artifact.version, msg.artifacts[1].version);
}

ZTEST(spotflow_ota_state, test_rehydration_rejects_mismatched_artifact_count)
{
	const struct spotflow_ota_persisted_attempt persisted = {
		.attempt_id = 1,
		.artifact_count = 2,
		.artifact_results = {
			SPOTFLOW_OTA_RESULT_SUCCEEDED,
			SPOTFLOW_OTA_RESULT_PENDING,
		},
	};
	struct spotflow_ota_update_msg msg = make_update(1, 1);
	struct spotflow_ota_update_result result;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_init_from_persistence(&persisted, true, NULL, false));
	zassert_not_ok(spotflow_ota_state_accept_update(&msg, &result));
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_accept_first_attempt_and_ignore_duplicate)
{
	struct spotflow_ota_update_msg msg = make_update(1, 2);
	struct spotflow_ota_update_result result;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	int rc = spotflow_ota_state_accept_update(&msg, &result);

	zassert_ok(rc);
	zassert_equal(result.disposition, SPOTFLOW_OTA_UPDATE_STARTED);
	zassert_true((result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
	zassert_equal(result.current_attempt_id, 1);

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

	rc = spotflow_ota_state_accept_update(&msg, &result);

	zassert_ok(rc);
	zassert_equal(result.disposition, SPOTFLOW_OTA_UPDATE_DUPLICATE);
	zassert_equal(result.effects, 0);
}

ZTEST(spotflow_ota_state, test_stale_worker_job_cannot_mutate_replaced_attempt)
{
	struct spotflow_ota_update_msg msg = make_update(1, 1);
	struct spotflow_ota_update_result update_result;
	struct spotflow_ota_rejection_result rejection_result;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job stale_job;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &update_result));
	zassert_true(spotflow_ota_state_get_worker_job(&stale_job));
	const struct spotflow_ota_operation_token stale_token =
		operation_token_from_job(&stale_job);
	zassert_ok(spotflow_ota_state_reject_update(msg.attempt_id,
						    SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE,
						    &rejection_result));

	zassert_equal(
		spotflow_ota_state_stage_artifact_result(&stale_job, SPOTFLOW_OTA_RESULT_SUCCEEDED),
		-ESTALE);
	zassert_equal(spotflow_ota_state_commit_artifact_result(&stale_job), -ESTALE);
	zassert_equal(spotflow_ota_state_fail_worker_operation(&stale_token), -ESTALE);

	stale_job.type = SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT;
	zassert_equal(spotflow_ota_state_commit_rejected_attempt(&stale_job), -ESTALE);
	zassert_equal(spotflow_ota_state_commit_attempt_finalization(&stale_job), -ESTALE);

	stale_job.type = SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT;
	zassert_equal(spotflow_ota_state_complete_report_job(&stale_job, true), -ESTALE);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.current_attempt_id, msg.attempt_id);
	zassert_not_equal(snapshot.current_attempt_generation, stale_job.generation);
	zassert_true(snapshot.has_attempt_error);
	zassert_equal(snapshot.attempt_error, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
}

ZTEST(spotflow_ota_state, test_stale_main_completion_cannot_resolve_replacement_probation)
{
	const struct spotflow_ota_persisted_attempt persisted = {
		.attempt_id = 1,
		.artifact_count = 1,
		.artifact_results = { SPOTFLOW_OTA_RESULT_PENDING },
	};
	const struct spotflow_ota_probation probation = {
		.attempt_id = 1,
		.artifact_index = 0,
		.slug = "main",
		.version = "1.0.0",
	};
	struct spotflow_ota_rejection_result rejection_result;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job stale_job;
	spotflow_ota_state_effects effects;

	zassert_ok(spotflow_ota_state_init_from_persistence(&persisted, true, &probation, true));
	zassert_ok(spotflow_ota_state_queue_main_firmware_result(
		persisted.attempt_id, probation.artifact_index, SPOTFLOW_OTA_RESULT_SUCCEEDED, NULL,
		&effects));
	zassert_true(spotflow_ota_state_get_worker_job(&stale_job));
	zassert_equal(stale_job.type, SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE);
	const struct spotflow_ota_operation_token stale_token =
		operation_token_from_job(&stale_job);

	zassert_ok(spotflow_ota_state_reject_update(persisted.attempt_id,
						    SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE,
						    &rejection_result));
	zassert_equal(spotflow_ota_state_resolve_main_firmware_probation(&stale_token), -ESTALE);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.current_attempt_id, persisted.attempt_id);
	zassert_not_equal(snapshot.current_attempt_generation, stale_token.generation);
	zassert_true(snapshot.has_attempt_error);
	zassert_equal(snapshot.attempt_error, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
}

ZTEST(spotflow_ota_state, test_reject_first_attempt_with_attempt_error)
{
	struct spotflow_ota_rejection_result result;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	int rc = spotflow_ota_state_reject_update(
		7, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE, &result);

	zassert_ok(rc);
	zassert_equal(result.disposition, SPOTFLOW_OTA_REJECTION_STARTED);
	zassert_true((result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);

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

ZTEST(spotflow_ota_state, test_duplicate_update_after_rejected_attempt_requests_report)
{
	struct spotflow_ota_update_msg msg = make_update(7, 1);
	struct spotflow_ota_rejection_result rejection_result;
	struct spotflow_ota_update_result update_result;

	zassert_ok(spotflow_ota_state_reject_update(
		7, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE, &rejection_result));

	int rc = spotflow_ota_state_accept_update(&msg, &update_result);

	zassert_ok(rc);
	zassert_equal(update_result.disposition, SPOTFLOW_OTA_UPDATE_DUPLICATE);
	zassert_true((update_result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
}

ZTEST(spotflow_ota_state, test_accept_cancel_before_success)
{
	struct spotflow_ota_update_msg msg = make_update(2, 2);
	struct spotflow_ota_update_result update_result;
	struct spotflow_ota_cancel_result cancel_result;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &update_result));

	int rc = spotflow_ota_state_accept_cancel(2, &cancel_result);

	zassert_ok(rc);
	zassert_equal(cancel_result.disposition, SPOTFLOW_OTA_CANCEL_ACCEPTED);
	zassert_true((cancel_result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
	zassert_true((cancel_result.effects &
		      SPOTFLOW_OTA_STATE_EFFECT_NOTIFY_CUSTOM_FIRMWARE_CANCELED) != 0);
	zassert_true((cancel_result.effects &
		      SPOTFLOW_OTA_STATE_EFFECT_CANCEL_MAIN_FIRMWARE_DOWNLOAD) != 0);
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
	struct spotflow_ota_update_result update_result;
	struct spotflow_ota_cancel_result cancel_result;
	spotflow_ota_state_effects effects;
	struct spotflow_ota_state_snapshot snapshot;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &update_result));
	zassert_ok(spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_SUCCEEDED,
							    &effects));

	int rc = spotflow_ota_state_accept_cancel(3, &cancel_result);

	zassert_ok(rc);
	zassert_equal(cancel_result.disposition, SPOTFLOW_OTA_CANCEL_IGNORED_LATE);
	zassert_equal(cancel_result.effects, 0);
	zassert_false(spotflow_ota_state_is_update_canceled());

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_false(snapshot.actionable_cancellation);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_SUCCEEDED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
}

ZTEST(spotflow_ota_state, test_running_artifact_result_is_preserved_after_cancel)
{
	struct spotflow_ota_update_msg msg = make_update(11, 2);
	struct spotflow_ota_update_result update_result;
	struct spotflow_ota_cancel_result cancel_result;
	spotflow_ota_state_effects effects;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &update_result));
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.artifact_index, 0);

	zassert_ok(spotflow_ota_state_accept_cancel(11, &cancel_result));
	zassert_equal(cancel_result.disposition, SPOTFLOW_OTA_CANCEL_ACCEPTED);
	zassert_false((cancel_result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_true(snapshot.actionable_cancellation);

	zassert_ok(spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_SUCCEEDED,
							    &effects));

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_SUCCEEDED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_false(snapshot.actionable_cancellation);
	zassert_true((effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
	zassert_false(spotflow_ota_state_is_update_canceled());
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.artifact_index, 1);
}

ZTEST(spotflow_ota_state, test_running_artifact_failure_after_cancel_cancels_remaining_artifacts)
{
	struct spotflow_ota_update_msg msg = make_update(12, 3);
	struct spotflow_ota_update_result update_result;
	struct spotflow_ota_cancel_result cancel_result;
	spotflow_ota_state_effects effects;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &update_result));
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.artifact_index, 0);

	zassert_ok(spotflow_ota_state_accept_cancel(12, &cancel_result));
	zassert_equal(cancel_result.disposition, SPOTFLOW_OTA_CANCEL_ACCEPTED);
	zassert_false((cancel_result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);

	zassert_ok(
		spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_FAILED, &effects));
	zassert_false((effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
	zassert_true(spotflow_ota_state_is_update_canceled());

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.artifact_results[2], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_true(snapshot.actionable_cancellation);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_update_artifacts_with_is_canceled_finishes_attempt)
{
	struct spotflow_ota_update_msg msg = make_update(4, 2);
	struct spotflow_ota_update_result result;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	msg.is_canceled = true;

	int rc = spotflow_ota_state_accept_update(&msg, &result);

	zassert_ok(rc);
	zassert_equal(result.disposition, SPOTFLOW_OTA_UPDATE_STARTED);
	zassert_true((result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
	zassert_true(spotflow_ota_state_is_update_canceled());

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_unknown_canceled_update_wakes_worker)
{
	struct spotflow_ota_update_msg msg = make_update(99, 3);
	struct spotflow_ota_update_result result;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	msg.is_canceled = true;

	int rc = spotflow_ota_state_accept_update(&msg, &result);

	zassert_ok(rc);
	zassert_equal(result.disposition, SPOTFLOW_OTA_UPDATE_STARTED);
	zassert_true((result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
	zassert_equal(result.current_attempt_id, 99);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_true(snapshot.has_current_attempt);
	zassert_equal(snapshot.current_attempt_id, 99);
	zassert_equal(snapshot.artifact_count, 3);
	zassert_true(snapshot.actionable_cancellation);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.artifact_results[2], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_canceled_update_after_finished_attempt_wakes_worker)
{
	struct spotflow_ota_update_msg finished = make_update(20, 1);
	struct spotflow_ota_update_msg canceled = make_update(21, 2);
	struct spotflow_ota_update_result result;
	spotflow_ota_state_effects effects;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&finished, &result));
	zassert_ok(spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_SUCCEEDED,
							    &effects));

	canceled.is_canceled = true;

	int rc = spotflow_ota_state_accept_update(&canceled, &result);

	zassert_ok(rc);
	zassert_equal(result.disposition, SPOTFLOW_OTA_UPDATE_STARTED);
	zassert_true((result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
	zassert_equal(result.current_attempt_id, 21);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.current_attempt_id, 21);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_report_request_matches_current_attempt_only)
{
	struct spotflow_ota_update_msg msg = make_update(5, 1);
	struct spotflow_ota_update_result update_result;
	struct spotflow_ota_report_result report_result;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &update_result));

	int rc = spotflow_ota_state_accept_report_request(6, &report_result);

	zassert_ok(rc);
	zassert_equal(report_result.disposition, SPOTFLOW_OTA_REPORT_NOT_CURRENT);
	zassert_equal(report_result.effects, 0);

	rc = spotflow_ota_state_accept_report_request(5, &report_result);

	zassert_ok(rc);
	zassert_equal(report_result.disposition, SPOTFLOW_OTA_REPORT_REQUESTED);
	zassert_true((report_result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);

	struct spotflow_ota_worker_job job;
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.type, SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT);
	zassert_equal(job.attempt_id, 5);
}

ZTEST(spotflow_ota_state, test_report_requests_coalesce_while_report_is_claimed)
{
	struct spotflow_ota_update_msg msg = make_update(5, 1);
	struct spotflow_ota_update_result update_result;
	struct spotflow_ota_report_result report_result;
	struct spotflow_ota_worker_job first_job;
	struct spotflow_ota_worker_job second_job;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &update_result));
	zassert_ok(spotflow_ota_state_accept_report_request(msg.attempt_id, &report_result));
	zassert_true(spotflow_ota_state_get_worker_job(&first_job));
	zassert_equal(first_job.type, SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT);

	zassert_ok(spotflow_ota_state_accept_report_request(msg.attempt_id, &report_result));
	zassert_ok(spotflow_ota_state_complete_report_job(&first_job, true));
	zassert_true(spotflow_ota_state_get_worker_job(&second_job));
	zassert_equal(second_job.type, SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT);
	zassert_ok(spotflow_ota_state_complete_report_job(&second_job, false));
	zassert_true(spotflow_ota_state_get_worker_job(&second_job));
	zassert_equal(second_job.type, SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT);

	zassert_ok(spotflow_ota_state_accept_report_request(msg.attempt_id, &report_result));
	zassert_true(spotflow_ota_state_get_worker_job(&second_job));
	zassert_equal(second_job.type, SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT);
}

ZTEST(spotflow_ota_state, test_ignore_duplicate_update_after_terminal_failure)
{
	struct spotflow_ota_update_msg msg = make_update(13, 2);
	struct spotflow_ota_update_result update_result;
	spotflow_ota_state_effects effects;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;
	int rc;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &update_result));
	zassert_ok(
		spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_FAILED, &effects));

	rc = spotflow_ota_state_accept_update(&msg, &update_result);

	zassert_ok(rc);
	zassert_equal(update_result.disposition, SPOTFLOW_OTA_UPDATE_DUPLICATE);
	zassert_true((update_result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.current_attempt_id, 13);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_false(snapshot.actionable_cancellation);
	zassert_false(spotflow_ota_state_is_update_canceled());
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.type, SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_duplicate_update_with_partial_results_requests_report)
{
	struct spotflow_ota_update_msg msg = make_update(20, 2);
	struct spotflow_ota_update_result update_result;
	spotflow_ota_state_effects effects;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;
	int rc;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &update_result));
	zassert_ok(spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_SUCCEEDED,
							    &effects));

	rc = spotflow_ota_state_accept_update(&msg, &update_result);

	zassert_ok(rc);
	zassert_equal(update_result.disposition, SPOTFLOW_OTA_UPDATE_DUPLICATE);
	zassert_true((update_result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.current_attempt_id, 20);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_SUCCEEDED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.type, SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT);
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.type, SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT);
	zassert_equal(job.artifact_index, 1);
}

ZTEST(spotflow_ota_state, test_accept_new_update_after_terminal_failure)
{
	struct spotflow_ota_update_msg first = make_update(14, 1);
	struct spotflow_ota_update_msg second = make_update(15, 1);
	struct spotflow_ota_update_result update_result;
	spotflow_ota_state_effects effects;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&first, &update_result));
	zassert_ok(
		spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_FAILED, &effects));

	int rc = spotflow_ota_state_accept_update(&second, &update_result);

	zassert_ok(rc);
	zassert_equal(update_result.disposition, SPOTFLOW_OTA_UPDATE_STARTED);
	zassert_true((update_result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
	zassert_equal(update_result.current_attempt_id, 15);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.current_attempt_id, 15);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.attempt_id, 15);
}

ZTEST(spotflow_ota_state, test_failed_artifact_cancels_remaining_artifacts)
{
	struct spotflow_ota_update_msg msg = make_update(8, 3);
	struct spotflow_ota_update_result update_result;
	spotflow_ota_state_effects effects;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &update_result));

	int rc = spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_FAILED, &effects);

	zassert_ok(rc);
	zassert_false((effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.artifact_results[2], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_false(snapshot.actionable_cancellation);
	zassert_false(spotflow_ota_state_is_update_canceled());
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_handler_canceled_without_cloud_cancel_does_not_action_cancel)
{
	struct spotflow_ota_update_msg msg = make_update(16, 2);
	struct spotflow_ota_update_result update_result;
	spotflow_ota_state_effects effects;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&msg, &update_result));
	zassert_true(spotflow_ota_state_get_worker_job(&job));

	zassert_ok(spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_CANCELED,
							    &effects));
	zassert_false((effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
	zassert_false(spotflow_ota_state_is_update_canceled());

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_false(snapshot.actionable_cancellation);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_reject_unfinished_attempt_defers_rejection_until_promoted)
{
	struct spotflow_ota_update_msg first = make_update(9, 2);
	struct spotflow_ota_update_result update_result;
	struct spotflow_ota_rejection_result rejection_result;
	spotflow_ota_state_effects effects;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&first, &update_result));
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.artifact_index, 0);

	int rc = spotflow_ota_state_reject_update(
		10, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE, &rejection_result);

	zassert_ok(rc);
	zassert_equal(rejection_result.disposition, SPOTFLOW_OTA_REJECTION_QUEUED);
	zassert_equal(rejection_result.current_attempt_id, 9);
	zassert_equal(rejection_result.pending_attempt_id, 10);
	zassert_true(spotflow_ota_state_is_update_canceled());

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.current_attempt_id, 9);
	zassert_true(snapshot.has_pending_attempt);
	zassert_equal(snapshot.pending_attempt_id, 10);
	zassert_true(snapshot.pending_is_rejection);
	zassert_equal(snapshot.pending_rejection_error,
		      SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_false(spotflow_ota_state_get_worker_job(&job));

	zassert_ok(spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_SUCCEEDED,
							    &effects));

	rc = spotflow_ota_state_promote_pending();

	zassert_ok(rc);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.current_attempt_id, 10);
	zassert_false(snapshot.has_pending_attempt);
	zassert_true(snapshot.has_attempt_error);
	zassert_equal(snapshot.attempt_error, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);

	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.type, SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT);
	zassert_equal(job.attempt_id, 10);
	zassert_equal(job.attempt_error, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_state, test_supersede_unfinished_attempt_and_promote_pending)
{
	struct spotflow_ota_update_msg first = make_update(9, 2);
	struct spotflow_ota_update_msg second = make_update(10, 1);
	struct spotflow_ota_update_result result;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&first, &result));

	int rc = spotflow_ota_state_accept_update(&second, &result);

	zassert_ok(rc);
	zassert_equal(result.disposition, SPOTFLOW_OTA_UPDATE_QUEUED);
	zassert_equal(result.current_attempt_id, 9);
	zassert_equal(result.pending_attempt_id, 10);
	zassert_true((result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
	zassert_true(spotflow_ota_state_is_update_canceled());

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.current_attempt_id, 9);
	zassert_true(snapshot.has_pending_attempt);
	zassert_equal(snapshot.pending_attempt_id, 10);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);

	rc = spotflow_ota_state_promote_pending();

	zassert_ok(rc);

	spotflow_ota_state_get_snapshot(&snapshot);

	zassert_equal(snapshot.current_attempt_id, 10);
	zassert_false(snapshot.has_pending_attempt);
	zassert_false(snapshot.actionable_cancellation);
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.attempt_id, 10);
	zassert_equal(job.artifact_index, 0);
}

ZTEST(spotflow_ota_state, test_cancel_is_ignored_after_main_upgrade_commit_starts)
{
	struct spotflow_ota_update_msg update = make_update(30, 2);
	struct spotflow_ota_update_result update_result;
	struct spotflow_ota_cancel_result cancel_result;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&update, &update_result));
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_ok(spotflow_ota_state_store_main_firmware_artifact(
		update.attempt_id, job.artifact_index, &update.artifacts[job.artifact_index]));
	zassert_ok(spotflow_ota_state_set_main_firmware_phase(SPOTFLOW_OTA_PHASE_PENDING_UPGRADE,
							      NULL));
	zassert_ok(spotflow_ota_state_begin_main_firmware_upgrade_commit());

	zassert_ok(spotflow_ota_state_accept_cancel(update.attempt_id, &cancel_result));
	zassert_equal(cancel_result.disposition, SPOTFLOW_OTA_CANCEL_IGNORED_LATE);
	zassert_equal(cancel_result.effects, 0);
	zassert_false(spotflow_ota_state_is_update_canceled());

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
}

ZTEST(spotflow_ota_state, test_supersession_does_not_mutate_attempt_after_upgrade_commit)
{
	struct spotflow_ota_update_msg current = make_update(30, 2);
	struct spotflow_ota_update_msg newer = make_update(31, 1);
	struct spotflow_ota_update_result result;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&current, &result));
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_ok(spotflow_ota_state_store_main_firmware_artifact(
		current.attempt_id, job.artifact_index, &current.artifacts[job.artifact_index]));
	zassert_ok(spotflow_ota_state_set_main_firmware_phase(SPOTFLOW_OTA_PHASE_PENDING_UPGRADE,
							      NULL));
	zassert_ok(spotflow_ota_state_begin_main_firmware_upgrade_commit());

	zassert_ok(spotflow_ota_state_accept_update(&newer, &result));
	zassert_equal(result.disposition, SPOTFLOW_OTA_UPDATE_QUEUED);
	zassert_false((result.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER) != 0);
	zassert_true((result.effects & SPOTFLOW_OTA_STATE_EFFECT_CANCEL_MAIN_FIRMWARE_DOWNLOAD) !=
		     0);
	zassert_false(spotflow_ota_state_is_update_canceled());

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_true(snapshot.has_pending_attempt);
	zassert_equal(snapshot.pending_attempt_id, newer.attempt_id);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
}

ZTEST_SUITE(spotflow_ota_state, NULL, NULL, before_each, NULL, NULL);
