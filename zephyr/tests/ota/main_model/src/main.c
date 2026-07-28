#include <string.h>

#include <zephyr/ztest.h>

#include "ota/core/spotflow_ota_main_model.h"
#include "ota/persistence/spotflow_ota_records_cbor.h"

static struct spotflow_ota_artifact make_main_artifact(void)
{
	struct spotflow_ota_artifact artifact = {
		.is_main = true,
	};

	strcpy(artifact.slug, "main");
	strcpy(artifact.version, "2.0.0");
	strcpy(artifact.url, "https://example.com/firmware.bin");
	strcpy(artifact.secret, "secret");
	return artifact;
}

static void claim_and_download(struct ota_main_firmware_model* main)
{
	struct spotflow_ota_artifact artifact = make_main_artifact();

	zassert_ok(spotflow_ota_main_claim(main, 1, &artifact, true, NULL));
	zassert_ok(spotflow_ota_main_download_pending(main, true, NULL));
	zassert_ok(spotflow_ota_main_download_started(main, true, NULL));
	zassert_ok(spotflow_ota_main_download_completed(main, true, NULL));
}

ZTEST(spotflow_ota_main_model, test_upgrade_transition_sequence)
{
	struct ota_main_firmware_model main;
	struct ota_main_prereboot_transition transition;

	spotflow_ota_main_clear(&main);
	zassert_true(spotflow_ota_main_is_valid(&main));
	claim_and_download(&main);
	zassert_true(spotflow_ota_main_is_valid(&main));

	zassert_ok(spotflow_ota_main_begin_upgrade_commit(&main, true, false));
	zassert_true(spotflow_ota_main_upgrade_is_irreversible(&main));
	zassert_ok(spotflow_ota_main_cancel_upgrade_commit(&main));
	zassert_false(spotflow_ota_main_upgrade_is_irreversible(&main));
	zassert_ok(spotflow_ota_main_begin_upgrade_commit(&main, true, false));
	zassert_ok(spotflow_ota_main_finish_prereboot(&main, true, &transition));
	zassert_true(transition.clear_artifact_transaction);
	zassert_equal(transition.state.phase, SPOTFLOW_OTA_PHASE_PENDING_REBOOT);
	zassert_true(spotflow_ota_main_probation_is_pending(&main));
	zassert_ok(spotflow_ota_main_begin_reboot(&main));
	zassert_true(spotflow_ota_main_is_valid(&main));
}

ZTEST(spotflow_ota_main_model, test_control_transition_table)
{
	struct {
		enum spotflow_ota_phase phase;
		enum ota_main_upgrade_state upgrade;
		bool pause_allowed;
		bool abort_allowed;
	} cases[] = {
		{ SPOTFLOW_OTA_PHASE_NOT_RUNNING, OTA_MAIN_UPGRADE_IDLE, false, false },
		{ SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD, OTA_MAIN_UPGRADE_HANDLER_ACTIVE, true,
		  true },
		{ SPOTFLOW_OTA_PHASE_DOWNLOADING, OTA_MAIN_UPGRADE_HANDLER_ACTIVE, true, true },
		{ SPOTFLOW_OTA_PHASE_PENDING_UPGRADE, OTA_MAIN_UPGRADE_HANDLER_ACTIVE, true, true },
		{ SPOTFLOW_OTA_PHASE_PENDING_REBOOT, OTA_MAIN_UPGRADE_REBOOT_READY, true, false },
		{ SPOTFLOW_OTA_PHASE_UNCONFIRMED, OTA_MAIN_UPGRADE_IDLE, false, false },
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		struct ota_main_firmware_model main;
		spotflow_ota_main_clear(&main);
		main.presence = OTA_MAIN_FIRMWARE_PRESENT;
		main.status.phase = cases[i].phase;
		main.upgrade = cases[i].upgrade;
		if (cases[i].upgrade == OTA_MAIN_UPGRADE_REBOOT_READY) {
			main.probation.state = OTA_MAIN_PROBATION_PENDING;
		}

		int rc = spotflow_ota_main_set_paused(&main, true, true, NULL);
		zassert_equal(rc == 0, cases[i].pause_allowed, "pause case %zu", i);
		if (rc == 0) {
			zassert_ok(spotflow_ota_main_set_paused(&main, true, false, NULL));
		}

		rc = spotflow_ota_main_request_abort(&main, true, NULL);
		zassert_equal(rc == 0, cases[i].abort_allowed, "abort case %zu", i);
		zassert_equal(spotflow_ota_main_is_abort_requested(&main, true),
			      cases[i].abort_allowed, "abort query case %zu", i);
	}
}

ZTEST(spotflow_ota_main_model, test_probation_reconciliation_sequence)
{
	struct ota_main_firmware_model main;
	struct spotflow_ota_probation probation = {
		.attempt_id = 42,
		.artifact_index = 1,
	};
	struct ota_main_completion_transition transition;

	strcpy(probation.slug, "main");
	strcpy(probation.version, "2.0.0");
	spotflow_ota_main_restore_probation(&main, &probation, true);
	zassert_true(spotflow_ota_main_probation_is_pending(&main));
	zassert_ok(spotflow_ota_main_enter_unconfirmed(&main, 1, NULL));
	zassert_ok(spotflow_ota_main_queue_reconciled_result(&main, 1,
							     SPOTFLOW_OTA_RESULT_SUCCEEDED, NULL));
	zassert_true(spotflow_ota_main_claim_completion(&main, &transition));
	zassert_equal(transition.artifact_index, 1);
	zassert_equal(transition.result, SPOTFLOW_OTA_RESULT_SUCCEEDED);
	zassert_true(transition.claim_artifact_transaction);
	zassert_ok(spotflow_ota_main_commit_probation_cleared(&main, true));
	zassert_false(spotflow_ota_main_probation_is_pending(&main));
	zassert_true(spotflow_ota_main_is_valid(&main));
}

ZTEST(spotflow_ota_main_model, test_handler_failure_resets_active_state)
{
	struct ota_main_firmware_model main;
	struct spotflow_ota_main_firmware_state state;
	struct spotflow_ota_artifact artifact = make_main_artifact();

	spotflow_ota_main_clear(&main);
	zassert_ok(spotflow_ota_main_claim(&main, 0, &artifact, true, NULL));
	zassert_ok(spotflow_ota_main_download_pending(&main, true, NULL));
	zassert_ok(spotflow_ota_main_set_paused(&main, true, true, NULL));
	zassert_ok(spotflow_ota_main_request_abort(&main, true, NULL));
	zassert_ok(spotflow_ota_main_fail_handler(&main, &state));
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
	zassert_equal(state.result, SPOTFLOW_OTA_RESULT_FAILED);
	zassert_false(state.is_paused);
	zassert_false(spotflow_ota_main_is_abort_requested(&main, true));
	zassert_true(spotflow_ota_main_is_valid(&main));
}

ZTEST_SUITE(spotflow_ota_main_model, NULL, NULL, NULL, NULL, NULL);
