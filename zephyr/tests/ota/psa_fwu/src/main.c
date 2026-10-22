#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "ota/firmware/spotflow_ota_fw_main.h"
#include "ota/platform/psa/spotflow_ota_psa_provider.h"
#include "ota/platform/spotflow_ota_identity_stream.h"
#include "ota/persistence/spotflow_ota_persistence.h"
#include "ota/protocol/spotflow_ota_net.h"
#include "spotflow_ota_bindesc_test_util.h"
#include "spotflow_ota_build_id_fake.h"
#include "spotflow_ota_downloader_transport_fake.h"
#include "spotflow_ota_test_fakes.h"
#include "spotflow_ota_test_settings.h"
#include "psa_fake.h"

LOG_MODULE_REGISTER(spotflow_ota, CONFIG_LOG_DEFAULT_LEVEL);

static bool abort_after_download;
static uint8_t image[188];
static uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];
static struct spotflow_ota_downloader_transport_fake* transport;

void spotflow_on_main_firmware_update_progressed(
	const struct spotflow_ota_main_firmware_state* state)
{
	if (abort_after_download && state->phase == SPOTFLOW_OTA_PHASE_PENDING_UPGRADE) {
		zassert_ok(spotflow_ota_fw_main_fail_update(NULL));
	}
}

bool spotflow_is_update_canceled(void)
{
	return spotflow_ota_state_is_update_canceled();
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);
	abort_after_download = false;
	spotflow_ota_test_settings_reset();
	spotflow_ota_test_fakes_reset();
	spotflow_ota_build_id_fake_reset(spotflow_ota_build_id_fake_get());
	spotflow_ota_net_reset();
	spotflow_ota_state_reset();
	spotflow_ota_fw_main_reset();
	zassert_ok(spotflow_ota_persistence_init());
	psa_fake_reset();
	(void)spotflow_ota_psa_abort();
	struct spotflow_ota_fw_main_startup_result startup;

	zassert_ok(spotflow_ota_fw_main_reconcile_startup(NULL, false, &startup));
	fake.init_count = 0;
	fake.clean_count = 0;
	fake.cancel_count = 0;
	for (size_t i = 0; i < sizeof(build_id); i++) {
		build_id[i] = 0x10 + i;
	}
	PSA_FWU_GPESlot_t manifest = {
		.Manifest_Integrity.Manifest_Integrity = { 1, 2, 3, 4 },
		.Manifest = {
			.GPE_Header = {
				.ih_magic.Magic_Number = 0x690c47c2,
				.ih_hdr_size = 32,
				.ih_img_size = 128,
				.ih_protect_tlv_size = 4,
			},
			.GPE_Version.iv_major = 1,
			.GPE_Magic.Magic_Number = 0x690c47c2,
		},
	};

	memset(image, 0xff, sizeof(image));
	memcpy(image, &manifest, sizeof(manifest));
	zassert_true(spotflow_ota_test_bindesc_write_build_id(image, sizeof(image), 64, build_id) >
		     0);
	sys_put_le16(0x6908, image + 176);
	sys_put_le16(4, image + 178);
	sys_put_le16(0x6907, image + 180);
	sys_put_le16(8, image + 182);
	transport = spotflow_ota_downloader_transport_fake_get();
	spotflow_ota_downloader_transport_fake_reset(transport);
	transport->payload = image;
	transport->payload_len = sizeof(image);
	transport->chunk_size = 1;
}

static enum spotflow_ota_result process(void)
{
	struct spotflow_ota_update_msg update = {
		.attempt_id = 42,
		.artifact_count = 1,
		.artifacts = { {
			.is_main = true,
			.slug = "main",
			.version = "cloud-version",
			.url = "https://example.com/vendor_image.sign.bin",
			.secret = "secret",
		} },
	};
	struct spotflow_ota_update_result accepted;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_accept_update(&update, &accepted));
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	return spotflow_ota_fw_main_process_artifact(&job);
}

static struct spotflow_ota_probation load_probation(void)
{
	struct spotflow_ota_probation probation;
	bool present;

	zassert_ok(spotflow_ota_persistence_load_probation(&probation, &present));
	zassert_true(present);
	return probation;
}

static int restore_and_reconcile(struct spotflow_ota_fw_main_startup_result* startup)
{
	struct spotflow_ota_probation probation = load_probation();
	struct spotflow_ota_persisted_attempt attempt = {
		.attempt_id = 42,
		.artifact_count = 1,
		.artifact_results = { SPOTFLOW_OTA_RESULT_PENDING },
	};

	zassert_ok(spotflow_ota_persistence_save_attempt(&attempt));
	zassert_ok(spotflow_ota_state_init_from_persistence(&attempt, true, &probation, true));
	return spotflow_ota_fw_main_reconcile_startup(&probation, true, startup);
}

static void expect_completion(enum spotflow_ota_result result)
{
	struct spotflow_ota_worker_job job;

	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.type, SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE);
	zassert_equal(job.token.attempt_id, 42);
	zassert_equal(job.data.complete_main_firmware.result, result);
	zassert_str_equal(job.data.complete_main_firmware.artifact.version, "cloud-version");
}

static void enter_trial(void)
{
	struct spotflow_ota_fw_main_startup_result startup;

	zassert_equal(process(), SPOTFLOW_OTA_RESULT_PENDING);
	psa_fake_trial(5);
	spotflow_ota_build_id_fake_set_running_build_id(build_id);
	zassert_ok(restore_and_reconcile(&startup));
	zassert_equal(startup.progress_state.phase, SPOTFLOW_OTA_PHASE_UNCONFIRMED);
}

ZTEST(spotflow_ota_psa, test_split_stream_installs_inactive_vendor_slot)
{
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(fake.target, 5);
	zassert_equal(fake.start_count, 1);
	zassert_equal(fake.finish_count, 1);
	zassert_equal(fake.install_count, 1);
	zassert_equal(fake.reboot_count, 1);
	zassert_mem_equal(fake.image, image, sizeof(image));
	struct spotflow_ota_probation probation = load_probation();

	zassert_mem_equal(probation.expected_build_id, build_id, sizeof(build_id));
	zassert_str_equal(probation.version, "cloud-version");
}

ZTEST(spotflow_ota_psa, test_second_update_uses_slot_one)
{
	psa_fake_committed(5);
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(fake.target, 4);
	zassert_equal(fake.clean_count, 1);
	zassert_equal(fake.reboot_count, 1);
}

ZTEST(spotflow_ota_psa, test_acceptance_keeps_probation_and_does_not_reboot)
{
	enter_trial();
	struct spotflow_ota_main_firmware_state state;
	spotflow_ota_state_effects effects;
	unsigned int reboots = fake.reboot_count;

	zassert_ok(spotflow_ota_fw_main_confirm_image(&state, &effects));
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_UNCONFIRMED);
	zassert_equal(state.result, SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(effects, 0);
	zassert_equal(fake.reboot_count, reboots);
	zassert_equal(fake.accept_count, 1);
	zassert_ok(spotflow_ota_fw_main_confirm_image(&state, &effects));
	zassert_equal(fake.accept_count, 1);
	struct spotflow_ota_worker_job job;

	zassert_false(spotflow_ota_state_get_worker_job(&job));
	(void)load_probation();

	/* Application requests commitment, then startup verifies the result. */
	zassert_equal(psa_fwu_request_reboot(), PSA_SUCCESS);
	psa_fake_committed(5);
	struct spotflow_ota_fw_main_startup_result startup;

	zassert_ok(restore_and_reconcile(&startup));
	zassert_equal(startup.progress_state.result, SPOTFLOW_OTA_RESULT_SUCCEEDED);
	expect_completion(SPOTFLOW_OTA_RESULT_SUCCEEDED);
}

ZTEST(spotflow_ota_psa, test_immediate_acceptance_queues_success)
{
	enter_trial();
	fake.accept_result = PSA_SUCCESS;
	struct spotflow_ota_main_firmware_state state;
	spotflow_ota_state_effects effects;

	zassert_ok(spotflow_ota_fw_main_confirm_image(&state, &effects));
	zassert_equal(state.result, SPOTFLOW_OTA_RESULT_SUCCEEDED);
	zassert_equal(fake.reboot_count, 1);
	expect_completion(SPOTFLOW_OTA_RESULT_SUCCEEDED);
}

ZTEST(spotflow_ota_psa, test_rollback_is_not_confused_with_committed_old_image)
{
	enter_trial();
	uint8_t old_id[SPOTFLOW_BUILD_ID_LENGTH] = { 99 };

	psa_fake_committed(4);
	spotflow_ota_build_id_fake_set_running_build_id(old_id);
	struct spotflow_ota_fw_main_startup_result startup;

	zassert_ok(restore_and_reconcile(&startup));
	expect_completion(SPOTFLOW_OTA_RESULT_FAILED);
}

ZTEST(spotflow_ota_psa, test_query_error_retains_probation)
{
	enter_trial();
	fake.query_error = PSA_ERROR_STORAGE_FAILURE;
	struct spotflow_ota_fw_main_startup_result startup;

	zassert_equal(restore_and_reconcile(&startup), -EIO);
	(void)load_probation();
	struct spotflow_ota_worker_job job;

	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST(spotflow_ota_psa, test_acceptance_error_does_not_complete_or_reboot)
{
	enter_trial();
	fake.accept_result = PSA_ERROR_NOT_PERMITTED;
	struct spotflow_ota_main_firmware_state state;
	spotflow_ota_state_effects effects;

	zassert_equal(spotflow_ota_fw_main_confirm_image(&state, &effects), -EPERM);
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_UNCONFIRMED);
	zassert_equal(fake.reboot_count, 1);
	zassert_equal(effects, 0);
	(void)load_probation();
}

ZTEST(spotflow_ota_psa, test_foreign_candidate_blocks_global_operation)
{
	fake.slots[3].state = PSA_FWU_CANDIDATE;
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(fake.start_count, 0);
	zassert_equal(fake.install_count, 0);
	zassert_equal(fake.clean_count, 0);
}

ZTEST(spotflow_ota_psa, test_trial_protects_rollback_storage_without_probation)
{
	psa_fake_trial(5);
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(fake.start_count, 0);
	zassert_equal(fake.clean_count, 0);
}

ZTEST(spotflow_ota_psa, test_truncated_trailer_is_canceled_and_cleaned)
{
	transport->payload_len -= 1;
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(fake.finish_count, 0);
	zassert_equal(fake.install_count, 0);
	zassert_equal(fake.cancel_count, 1);
	zassert_equal(fake.clean_count, 1);
}

ZTEST(spotflow_ota_psa, test_invalid_or_oversized_manifest_is_not_started)
{
	sys_put_le32(UINT32_MAX, image + 28);
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(fake.start_count, 0);
}

ZTEST(spotflow_ota_psa, test_bad_identity_does_not_install)
{
	memset(image + 76, 0, sizeof(build_id));
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(fake.finish_count, 0);
	zassert_equal(fake.install_count, 0);
	zassert_equal(fake.cancel_count, 1);
}

ZTEST(spotflow_ota_psa, test_write_error_cleans_incomplete_candidate)
{
	fake.write_error = PSA_ERROR_STORAGE_FAILURE;
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(fake.cancel_count, 1);
	zassert_equal(fake.clean_count, 1);
	zassert_equal(fake.reboot_count, 0);
}

ZTEST(spotflow_ota_psa, test_download_retry_preserves_prefix_and_offsets)
{
	transport->partial_transient_fail_after_bytes = 37;
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_PENDING);
	zassert_true(transport->call_count > 1);
	zassert_equal(fake.start_count, 1);
	zassert_mem_equal(fake.image, image, sizeof(image));
}

ZTEST(spotflow_ota_psa, test_failed_installation_reboot_retains_pending_record)
{
	fake.reboot_result = PSA_ERROR_STORAGE_FAILURE;
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_PENDING);
	(void)load_probation();
}

ZTEST(spotflow_ota_psa, test_provider_rejects_nonsequential_offsets)
{
	zassert_ok(spotflow_ota_psa_begin());
	struct spotflow_artifact_block block = {
		.offset = 1,
		.data = image,
		.data_len = 10,
	};

	zassert_equal(spotflow_ota_psa_write(&block), -EINVAL);
	zassert_equal(fake.start_count, 0);
}

ZTEST(spotflow_ota_psa, test_abort_after_download_cleans_writer)
{
	abort_after_download = true;
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(fake.cancel_count, 1);
	zassert_equal(fake.clean_count, 1);
	zassert_equal(fake.finish_count, 0);
	zassert_equal(fake.install_count, 0);
}

ZTEST(spotflow_ota_psa, test_probation_save_failure_does_not_install)
{
	spotflow_ota_test_settings_set_save_failure("spotflow/ota/probation");
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(fake.finish_count, 1);
	zassert_equal(fake.install_count, 0);
	zassert_equal(fake.cancel_count, 1);
}

ZTEST(spotflow_ota_psa, test_manifest_version_refusal_is_not_installed)
{
	fake.start_error = PSA_ERROR_NOT_PERMITTED;
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(fake.write_count, 0);
	zassert_equal(fake.install_count, 0);
}

ZTEST(spotflow_ota_psa, test_foreign_trial_is_not_accepted)
{
	enter_trial();
	fake.slots[3].state = PSA_FWU_TRIAL;
	struct spotflow_ota_main_firmware_state state;
	spotflow_ota_state_effects effects;

	zassert_equal(spotflow_ota_fw_main_confirm_image(&state, &effects), -EBUSY);
	zassert_equal(fake.accept_count, 0);
	zassert_equal(fake.reboot_count, 1);
}

ZTEST(spotflow_ota_psa, test_wrong_running_build_cannot_be_accepted)
{
	enter_trial();
	uint8_t wrong_id[SPOTFLOW_BUILD_ID_LENGTH] = { 1 };
	struct spotflow_ota_main_firmware_state state;
	spotflow_ota_state_effects effects;

	spotflow_ota_build_id_fake_set_running_build_id(wrong_id);
	zassert_equal(spotflow_ota_fw_main_confirm_image(&state, &effects), -EINVAL);
	zassert_equal(fake.accept_count, 0);
}

ZTEST(spotflow_ota_psa, test_bad_trailer_length_is_not_finished)
{
	sys_put_le16(12, image + 178);
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(fake.finish_count, 0);
	zassert_equal(fake.cancel_count, 1);
}

ZTEST(spotflow_ota_psa, test_same_executable_identity_is_rejected_before_probation)
{
	spotflow_ota_build_id_fake_set_running_build_id(build_id);
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(fake.finish_count, 0);
	zassert_equal(fake.install_count, 0);
	zassert_equal(fake.cancel_count, 1);
}

ZTEST(spotflow_ota_psa, test_orphan_staged_image_is_rejected_before_reuse)
{
	fake.slots[5].state = PSA_FWU_STAGED;
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(fake.reject_count, 1);
	zassert_equal(fake.clean_count, 1);
	zassert_equal(fake.target, 5);
}

ZTEST(spotflow_ota_psa, test_restored_cancellation_queues_result_report)
{
	struct spotflow_ota_persisted_attempt persisted = {
		.attempt_id = 42,
		.artifact_count = 1,
		.artifact_results = { SPOTFLOW_OTA_RESULT_PENDING },
	};
	struct spotflow_ota_update_msg update = {
		.attempt_id = 42,
		.is_canceled = true,
		.artifact_count = 1,
		.artifacts = { {
			.is_main = true,
			.slug = "main",
			.version = "cloud-version",
			.url = "https://example.com/vendor_image.sign.bin",
			.secret = "secret",
		} },
	};
	struct spotflow_ota_update_result accepted;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_state_init_from_persistence(&persisted, true, NULL, false));
	zassert_ok(spotflow_ota_state_accept_update(&update, &accepted));
	zassert_true(accepted.effects & SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER);
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.type, SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT);
	struct spotflow_ota_persistence_capture capture;

	zassert_ok(spotflow_ota_state_capture_persisted_attempt(
		&job, SPOTFLOW_OTA_PERSISTENCE_VIEW_DURABLE, &capture));
	zassert_equal(capture.attempt.artifact_results[0], SPOTFLOW_OTA_RESULT_CANCELED);
}

ZTEST_SUITE(spotflow_ota_psa, NULL, NULL, before_each, NULL, NULL);

ZTEST(spotflow_ota_psa, test_large_blocks_preserve_prefix_and_bound_psa_writes)
{
	uint8_t artifact[1024];

	memset(artifact, 0xff, sizeof(artifact));
	memcpy(artifact, image, 176);
	PSA_FWU_GPESlot_t prefix;

	memcpy(&prefix, artifact, sizeof(prefix));

	prefix.Manifest.GPE_Header.ih_img_size = 964;
	prefix.Manifest.GPE_Header.ih_protect_tlv_size = 4;
	memcpy(artifact, &prefix, sizeof(prefix));
	sys_put_le16(0x6908, artifact + 1012);
	sys_put_le16(4, artifact + 1014);
	sys_put_le16(0x6907, artifact + 1016);
	sys_put_le16(8, artifact + 1018);
	transport->payload = artifact;
	transport->payload_len = sizeof(artifact);
	transport->chunk_size = sizeof(artifact);
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(fake.start_count, 1);
	zassert_equal(fake.max_block_size, 256);
	zassert_mem_equal(fake.image, artifact, sizeof(artifact));
}

ZTEST(spotflow_ota_psa, test_signature_trailer_cannot_supply_executable_identity)
{
	uint8_t artifact[224];

	memset(artifact, 0xff, sizeof(artifact));
	memcpy(artifact, image, sizeof(image));
	memset(artifact + 48, 0xff, 128);
	sys_put_le16(44, artifact + 182);
	zassert_true(spotflow_ota_test_bindesc_write_build_id(artifact, sizeof(artifact), 188,
							      build_id));
	transport->payload = artifact;
	transport->payload_len = sizeof(artifact);
	transport->chunk_size = sizeof(artifact);
	zassert_equal(process(), SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(fake.finish_count, 0);
	zassert_equal(fake.install_count, 0);
}

ZTEST(spotflow_ota_psa, test_unexpected_running_state_fails_startup_without_probation)
{
	struct spotflow_ota_fw_main_startup_result startup;

	fake.slots[4].state = PSA_FWU_READY;
	zassert_equal(spotflow_ota_fw_main_reconcile_startup(NULL, false, &startup), -EIO);
	zassert_equal(fake.accept_count, 0);
	zassert_equal(fake.reboot_count, 0);
}
