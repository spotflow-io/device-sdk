#include "backend/minimal/spotflow_minimal_logging.h"

#include "config/spotflow_config.h"
#include "config/spotflow_config_options.h"
#include "net/spotflow_processor.h"
#include "net/spotflow_transport.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <zcbor_encode.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_core.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_msg.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/sys/cbprintf.h>

#ifdef CONFIG_SPOTFLOW_TELEMETRY_BACKEND_MINIMAL

#define KEY_MESSAGE_TYPE 0x00
#define KEY_BODY 0x01
#define KEY_SEVERITY 0x04
#define KEY_LABELS 0x05
#define KEY_DEVICE_UPTIME_MS 0x06
#define KEY_SEQUENCE_NUMBER 0x0D

#define LOG_MESSAGE_TYPE 0
#define LOG_MAP_ENTRIES 5
#define LOG_CBOR_STATE_DEPTH 2
#define LOG_SOURCE_MAX_LENGTH 32

enum slot_state {
	SLOT_EMPTY,
	SLOT_WRITING,
	SLOT_READY,
	SLOT_IN_FLIGHT,
};

struct log_slot {
	enum slot_state state;
	uint32_t sequence;
	uint32_t severity;
	uint32_t uptime_ms;
	const char* source;
	size_t body_len;
	char body[CONFIG_SPOTFLOW_LOG_BUFFER_SIZE];
};

struct format_context {
	struct log_slot* slot;
};

static struct log_slot slots[CONFIG_SPOTFLOW_LOG_BACKEND_QUEUE_SIZE];
static struct k_spinlock slots_lock;
static uint32_t next_sequence;

static void backend_init(const struct log_backend* backend);
static void backend_process(const struct log_backend* backend, union log_msg_generic* msg);
static void backend_panic(const struct log_backend* backend);
static void backend_dropped(const struct log_backend* backend, uint32_t count);
static struct log_slot* reserve_slot(void);
static struct log_slot* claim_oldest_ready(void);
static bool sequence_before(uint32_t lhs, uint32_t rhs);
static int format_character(int character, void* context);
static int encode_slot(const struct log_slot* slot, uint8_t* workspace, size_t workspace_size,
		       size_t* encoded_size);
static void set_slot_state(struct log_slot* slot, enum slot_state state);
static int restore_after_error(struct log_slot* slot, int error);
uint32_t spotflow_cbor_convert_log_level_to_severity(uint8_t level);
uint8_t spotflow_cbor_convert_severity_to_log_level(uint32_t severity);

static const struct log_backend_api log_backend_spotflow_api = {
	.init = backend_init,
	.process = backend_process,
	.panic = backend_panic,
	.dropped = backend_dropped,
};

LOG_BACKEND_DEFINE(log_backend_spotflow, log_backend_spotflow_api, true);

void spotflow_log_backend_try_set_runtime_filter(uint32_t level)
{
#ifdef CONFIG_SPOTFLOW_LOG_BACKEND_SET_RUNTIME_FILTERING
	for (uint32_t source = 0; source < log_src_cnt_get(0); source++) {
		log_filter_set(&log_backend_spotflow, 0, source, level);
	}
#else
	ARG_UNUSED(level);
#endif /* CONFIG_SPOTFLOW_LOG_BACKEND_SET_RUNTIME_FILTERING */
}

int spotflow_minimal_log_process(uint8_t* workspace, size_t workspace_size)
{
	if (workspace == NULL || workspace_size == 0) {
		return -EINVAL;
	}

	struct log_slot* slot = claim_oldest_ready();
	if (slot == NULL) {
		return 0;
	}

	size_t encoded_size;
	int rc = encode_slot(slot, workspace, workspace_size, &encoded_size);
	if (rc < 0) {
		set_slot_state(slot, SLOT_EMPTY);
		return rc;
	}

	rc = spotflow_transport_send_ingest_cbor(workspace, encoded_size);
	if (rc < 0) {
		return restore_after_error(slot, rc);
	}

	set_slot_state(slot, SLOT_EMPTY);
	return 1;
}

static void backend_init(const struct log_backend* backend)
{
	ARG_UNUSED(backend);

	k_spinlock_key_t key = k_spin_lock(&slots_lock);
	memset(slots, 0, sizeof(slots));
	next_sequence = 0;
	k_spin_unlock(&slots_lock, key);

	spotflow_config_init();
	spotflow_start_processing();
}

static void backend_process(const struct log_backend* backend, union log_msg_generic* msg)
{
	ARG_UNUSED(backend);

	struct log_msg* log_msg = &msg->log;
	uint8_t level = log_msg_get_level(log_msg);
	if (level > spotflow_config_get_sent_log_level()) {
		return;
	}

	struct log_slot* slot = reserve_slot();
	if (slot == NULL) {
		return;
	}

	slot->severity = spotflow_cbor_convert_log_level_to_severity(level);
	slot->uptime_ms =
		(uint32_t)(log_output_timestamp_to_us(log_msg_get_timestamp(log_msg)) / 1000U);
	int16_t source_id = log_msg_get_source_id(log_msg);
	slot->source = source_id >= 0
		? log_source_name_get(log_msg_get_domain(log_msg), (uint32_t)source_id)
		: "unknown";
	if (slot->source == NULL) {
		slot->source = "unknown";
	}

	size_t package_size;
	uint8_t* package = log_msg_get_package(log_msg, &package_size);
	ARG_UNUSED(package_size);
	struct format_context context = { .slot = slot };
	slot->body_len = 0;
	int rc = cbpprintf(format_character, &context, package);
	if (rc < 0 || slot->body_len >= sizeof(slot->body)) {
		set_slot_state(slot, SLOT_EMPTY);
		return;
	}

	slot->body[slot->body_len] = '\0';
	set_slot_state(slot, SLOT_READY);
}

static void backend_panic(const struct log_backend* backend)
{
	ARG_UNUSED(backend);
}

static void backend_dropped(const struct log_backend* backend, uint32_t count)
{
	ARG_UNUSED(backend);
	ARG_UNUSED(count);
}

static struct log_slot* reserve_slot(void)
{
	struct log_slot* selected = NULL;
	k_spinlock_key_t key = k_spin_lock(&slots_lock);

	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		if (slots[i].state == SLOT_EMPTY) {
			selected = &slots[i];
			break;
		}
	}

	if (selected == NULL) {
		for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
			if (slots[i].state == SLOT_READY &&
			    (selected == NULL ||
			     sequence_before(slots[i].sequence, selected->sequence))) {
				selected = &slots[i];
			}
		}
	}

	if (selected != NULL) {
		selected->state = SLOT_WRITING;
		selected->sequence = next_sequence++;
	}

	k_spin_unlock(&slots_lock, key);
	return selected;
}

static struct log_slot* claim_oldest_ready(void)
{
	struct log_slot* selected = NULL;
	k_spinlock_key_t key = k_spin_lock(&slots_lock);

	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		if (slots[i].state == SLOT_READY &&
		    (selected == NULL || sequence_before(slots[i].sequence, selected->sequence))) {
			selected = &slots[i];
		}
	}

	if (selected != NULL) {
		selected->state = SLOT_IN_FLIGHT;
	}

	k_spin_unlock(&slots_lock, key);
	return selected;
}

static bool sequence_before(uint32_t lhs, uint32_t rhs)
{
	return (int32_t)(lhs - rhs) < 0;
}

static int format_character(int character, void* context)
{
	struct log_slot* slot = ((struct format_context*)context)->slot;
	if (slot->body_len >= sizeof(slot->body) - 1) {
		return -ENOSPC;
	}

	slot->body[slot->body_len++] = (char)character;
	return 0;
}

static int encode_slot(const struct log_slot* slot, uint8_t* workspace, size_t workspace_size,
		       size_t* encoded_size)
{
	zcbor_state_t state[LOG_CBOR_STATE_DEPTH];
	zcbor_new_encode_state(state, ARRAY_SIZE(state), workspace, workspace_size, 1);

	bool success = zcbor_map_start_encode(state, LOG_MAP_ENTRIES);
	success = success && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	success = success && zcbor_uint32_put(state, LOG_MESSAGE_TYPE);
	success = success && zcbor_uint32_put(state, KEY_SEQUENCE_NUMBER);
	success = success && zcbor_uint32_put(state, slot->sequence);
	success = success && zcbor_uint32_put(state, KEY_SEVERITY);
	success = success && zcbor_uint32_put(state, slot->severity);
	success = success && zcbor_uint32_put(state, KEY_DEVICE_UPTIME_MS);
	success = success && zcbor_uint32_put(state, slot->uptime_ms);
	success = success && zcbor_uint32_put(state, KEY_LABELS);
	success = success && zcbor_map_start_encode(state, 1);
	success = success && zcbor_tstr_put_lit(state, "source");
	success = success && zcbor_tstr_put_term(state, slot->source, LOG_SOURCE_MAX_LENGTH);
	success = success && zcbor_map_end_encode(state, 1);
	success = success && zcbor_uint32_put(state, KEY_BODY);
	success = success && zcbor_tstr_put_term(state, slot->body, slot->body_len + 1);
	success = success && zcbor_map_end_encode(state, LOG_MAP_ENTRIES);
	if (!success) {
		return -EINVAL;
	}

	*encoded_size = (size_t)(state->payload - workspace);
	return 0;
}

static void set_slot_state(struct log_slot* slot, enum slot_state state)
{
	k_spinlock_key_t key = k_spin_lock(&slots_lock);
	slot->state = state;
	k_spin_unlock(&slots_lock, key);
}

static int restore_after_error(struct log_slot* slot, int error)
{
	set_slot_state(slot, SLOT_READY);
	if (error != -EAGAIN) {
		spotflow_transport_abort();
	}
	return error;
}

uint32_t spotflow_cbor_convert_log_level_to_severity(uint8_t level)
{
	switch (level) {
	case LOG_LEVEL_ERR:
		return 60;
	case LOG_LEVEL_WRN:
		return 50;
	case LOG_LEVEL_INF:
		return 40;
	case LOG_LEVEL_DBG:
		return 30;
	default:
		return 0;
	}
}

uint8_t spotflow_cbor_convert_severity_to_log_level(uint32_t severity)
{
	switch (severity) {
	case 70:
	case 60:
		return LOG_LEVEL_ERR;
	case 50:
		return LOG_LEVEL_WRN;
	case 40:
		return LOG_LEVEL_INF;
	case 30:
	default:
		return LOG_LEVEL_DBG;
	}
}

#endif /* CONFIG_SPOTFLOW_TELEMETRY_BACKEND_MINIMAL */
