#include <string.h>

#include <zephyr/ztest.h>

#include "ota/core/spotflow_ota_attempt_model.h"

static struct spotflow_ota_artifact make_artifact(size_t index)
{
	struct spotflow_ota_artifact artifact = {
		.is_main = index == 0,
	};

	strcpy(artifact.slug, index == 0 ? "main" : "custom");
	strcpy(artifact.version, index == 0 ? "2.0.0" : "3.0.0");
	strcpy(artifact.url, "https://example.com/firmware.bin");
	strcpy(artifact.secret, "secret");
	return artifact;
}

static struct spotflow_ota_update_msg make_update(uint64_t attempt_id, size_t artifact_count)
{
	struct spotflow_ota_update_msg update = {
		.attempt_id = attempt_id,
		.artifact_count = artifact_count,
	};

	for (size_t i = 0; i < artifact_count; i++) {
		update.artifacts[i] = make_artifact(i);
	}
	return update;
}

ZTEST(spotflow_ota_attempt_model, test_start_and_rehydrate_are_pure_model_transitions)
{
	struct ota_attempt_model attempt;
	uint32_t next_generation = 0;
	struct spotflow_ota_update_msg update = make_update(42, 2);

	zassert_ok(spotflow_ota_attempt_validate_update(&update));
	spotflow_ota_attempt_start(
		&update, spotflow_ota_attempt_allocate_generation(&next_generation), &attempt);
	zassert_true(
		spotflow_ota_attempt_is_valid(&attempt, (struct ota_attempt_constraints){ 0 }));
	zassert_equal(attempt.identity.generation, 1);
	zassert_equal(attempt.identity.lifecycle, OTA_ATTEMPT_ACTIVE);
	zassert_equal(attempt.execution.next_index, 0);

	attempt.plan.source = OTA_ARTIFACT_PLAN_PERSISTED_RESULTS;
	memset(attempt.plan.artifacts, 0, sizeof(attempt.plan.artifacts));
	spotflow_ota_attempt_refresh_lifecycle(&attempt, (struct ota_attempt_constraints){ 0 });

	bool request_report;
	bool wake_worker;
	zassert_ok(spotflow_ota_attempt_rehydrate(&update, &attempt,
						  (struct ota_attempt_constraints){ 0 },
						  &request_report, &wake_worker));
	zassert_equal(attempt.plan.source, OTA_ARTIFACT_PLAN_FULL_MANIFEST);
	zassert_false(request_report);
	zassert_true(wake_worker);
	zassert_true(
		spotflow_ota_attempt_is_valid(&attempt, (struct ota_attempt_constraints){ 0 }));
}

ZTEST(spotflow_ota_attempt_model, test_pending_attempt_is_a_tagged_union)
{
	struct ota_attempt_model attempt;
	struct ota_pending_attempt pending;
	uint32_t next_generation = 0;
	struct spotflow_ota_update_msg update = make_update(55, 1);

	spotflow_ota_pending_attempt_clear(&pending);
	spotflow_ota_pending_attempt_store_rejection(
		&pending, 54, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
	zassert_equal(spotflow_ota_pending_attempt_id(&pending), 54);
	zassert_true(spotflow_ota_attempt_promote_pending(
		&attempt, &pending, spotflow_ota_attempt_allocate_generation(&next_generation)));
	zassert_equal(attempt.identity.id, 54);
	zassert_equal(attempt.identity.lifecycle, OTA_ATTEMPT_REJECTING);
	zassert_equal(pending.kind, OTA_PENDING_ATTEMPT_NONE);

	spotflow_ota_pending_attempt_store_update(&pending, &update);
	zassert_true(spotflow_ota_attempt_promote_pending(
		&attempt, &pending, spotflow_ota_attempt_allocate_generation(&next_generation)));
	zassert_equal(attempt.identity.id, 55);
	zassert_equal(attempt.identity.generation, 2);
	zassert_equal(attempt.plan.source, OTA_ARTIFACT_PLAN_FULL_MANIFEST);
}

ZTEST(spotflow_ota_attempt_model, test_update_routing_table)
{
	struct {
		bool has_current;
		uint64_t incoming_id;
		enum ota_attempt_update_outcome expected;
	} cases[] = {
		{ false, 42, OTA_ATTEMPT_UPDATE_STARTED },
		{ true, 42, OTA_ATTEMPT_UPDATE_DUPLICATE },
		{ true, 43, OTA_ATTEMPT_UPDATE_QUEUED },
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		struct ota_attempt_model attempt;
		struct ota_pending_attempt pending;
		uint32_t next_generation = 0;
		struct spotflow_ota_update_msg current = make_update(42, 2);
		struct spotflow_ota_update_msg incoming = make_update(cases[i].incoming_id, 2);
		spotflow_ota_attempt_clear(&attempt);
		spotflow_ota_pending_attempt_clear(&pending);
		if (cases[i].has_current) {
			spotflow_ota_attempt_start(
				&current,
				spotflow_ota_attempt_allocate_generation(&next_generation),
				&attempt);
		}

		struct ota_attempt_update_transition transition;
		zassert_ok(spotflow_ota_attempt_accept_update(
			&attempt, &pending, &next_generation, &incoming,
			(struct ota_attempt_constraints){ 0 }, &transition));
		zassert_equal(transition.outcome, cases[i].expected, "case %zu", i);
		if (transition.outcome == OTA_ATTEMPT_UPDATE_QUEUED) {
			zassert_equal(spotflow_ota_pending_attempt_id(&pending),
				      incoming.attempt_id);
			zassert_true(transition.cancel_main_firmware);
			zassert_true(transition.wake_worker);
		}
	}
}

ZTEST(spotflow_ota_attempt_model, test_cancel_transition_table)
{
	struct {
		uint64_t cancel_id;
		bool irreversible;
		enum ota_attempt_cancel_outcome expected;
	} cases[] = {
		{ 41, false, OTA_ATTEMPT_CANCEL_NOT_CURRENT },
		{ 42, true, OTA_ATTEMPT_CANCEL_IGNORED_LATE },
		{ 42, false, OTA_ATTEMPT_CANCEL_ACCEPTED },
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		struct ota_attempt_model attempt;
		struct spotflow_ota_update_msg update = make_update(42, 2);
		spotflow_ota_attempt_start(&update, 1, &attempt);
		struct ota_attempt_cancel_transition transition =
			spotflow_ota_attempt_accept_cancel(
				&attempt, cases[i].cancel_id,
				(struct ota_attempt_constraints){
					.main_upgrade_irreversible = cases[i].irreversible,
				});
		zassert_equal(transition.outcome, cases[i].expected, "case %zu", i);
		if (transition.outcome == OTA_ATTEMPT_CANCEL_ACCEPTED) {
			zassert_true(transition.wake_worker);
			zassert_equal(attempt.plan.results[0], SPOTFLOW_OTA_RESULT_CANCELED);
			zassert_equal(attempt.plan.results[1], SPOTFLOW_OTA_RESULT_CANCELED);
			zassert_equal(attempt.identity.lifecycle, OTA_ATTEMPT_FINALIZING);
		}
	}
}

ZTEST(spotflow_ota_attempt_model, test_staged_result_is_projected_before_commit)
{
	struct ota_attempt_model attempt;
	struct spotflow_ota_update_msg update = make_update(42, 2);
	enum spotflow_ota_result projected[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
	struct ota_attempt_constraints constraints = { 0 };

	spotflow_ota_attempt_start(&update, 1, &attempt);
	attempt.execution.transaction.state = OTA_ARTIFACT_TRANSACTION_RUNNING;
	attempt.execution.transaction.artifact_index = 0;
	spotflow_ota_attempt_stage_result(&attempt, 0, SPOTFLOW_OTA_RESULT_FAILED,
					  OTA_ARTIFACT_RESULT_HANDLER);

	zassert_equal(attempt.plan.results[0], SPOTFLOW_OTA_RESULT_PENDING);
	spotflow_ota_attempt_project_results(&attempt, constraints, projected);
	zassert_equal(projected[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(projected[1], SPOTFLOW_OTA_RESULT_CANCELED);

	spotflow_ota_attempt_apply_staged_result(&attempt, constraints);
	zassert_equal(attempt.plan.results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(attempt.plan.results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_false(spotflow_ota_attempt_transaction_is_active(&attempt));
}

ZTEST(spotflow_ota_attempt_model, test_late_cancellation_advances_staged_revision)
{
	struct ota_attempt_model attempt;
	struct spotflow_ota_update_msg update = make_update(42, 2);

	spotflow_ota_attempt_start(&update, 1, &attempt);
	attempt.execution.transaction.state = OTA_ARTIFACT_TRANSACTION_RUNNING;
	attempt.execution.transaction.artifact_index = 0;
	spotflow_ota_attempt_stage_result(&attempt, 0, SPOTFLOW_OTA_RESULT_SUCCEEDED,
					  OTA_ARTIFACT_RESULT_HANDLER);

	struct ota_attempt_cancel_transition transition = spotflow_ota_attempt_accept_cancel(
		&attempt, 42, (struct ota_attempt_constraints){ 0 });
	zassert_equal(transition.outcome, OTA_ATTEMPT_CANCEL_ACCEPTED);
	zassert_false(transition.wake_worker);
	zassert_equal(attempt.execution.transaction.mutation_revision, 2);
	zassert_true(attempt.execution.transaction.mutation.cancel_remaining);
	zassert_equal(attempt.plan.results[0], SPOTFLOW_OTA_RESULT_PENDING);
}

ZTEST(spotflow_ota_attempt_model, test_invariant_rejects_cached_state_drift)
{
	struct ota_attempt_model valid;
	struct ota_attempt_model invalid;
	struct spotflow_ota_update_msg update = make_update(42, 2);
	struct ota_attempt_constraints constraints = { 0 };

	spotflow_ota_attempt_start(&update, 1, &valid);
	zassert_true(spotflow_ota_attempt_is_valid(&valid, constraints));

	invalid = valid;
	invalid.execution.next_index = 1;
	zassert_false(spotflow_ota_attempt_is_valid(&invalid, constraints));

	invalid = valid;
	invalid.execution.transaction.mutation_revision = 1;
	zassert_false(spotflow_ota_attempt_is_valid(&invalid, constraints));

	invalid = valid;
	invalid.identity.lifecycle = OTA_ATTEMPT_TERMINAL;
	zassert_false(spotflow_ota_attempt_is_valid(&invalid, constraints));

	invalid = valid;
	invalid.execution.transaction.state = OTA_ARTIFACT_TRANSACTION_RUNNING;
	invalid.execution.transaction.artifact_index = 0;
	invalid.plan.results[0] = SPOTFLOW_OTA_RESULT_SUCCEEDED;
	invalid.execution.next_index = 1;
	zassert_false(spotflow_ota_attempt_is_valid(&invalid, constraints));

	invalid = valid;
	invalid.execution.transaction.state = OTA_ARTIFACT_TRANSACTION_RUNNING;
	invalid.execution.transaction.artifact_index = 0;
	spotflow_ota_attempt_stage_result(&invalid, 0, SPOTFLOW_OTA_RESULT_FAILED,
					  OTA_ARTIFACT_RESULT_HANDLER);
	spotflow_ota_attempt_refresh_lifecycle(&invalid, constraints);
	zassert_true(spotflow_ota_attempt_is_valid(&invalid, constraints));
	invalid.identity.lifecycle = OTA_ATTEMPT_FINALIZATION_CLAIMED;
	zassert_false(spotflow_ota_attempt_is_valid(&invalid, constraints));
}

ZTEST_SUITE(spotflow_ota_attempt_model, NULL, NULL, NULL, NULL, NULL);
