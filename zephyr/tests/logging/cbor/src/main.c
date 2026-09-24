#include "logging/spotflow_log_cbor.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/cbprintf.h>
#include <zephyr/ztest.h>
#include <zcbor_decode.h>

LOG_MODULE_REGISTER(spotflow_logging, LOG_LEVEL_INF);

static struct spotflow_cbor_output_context context;
static uint8_t* encoded;
static size_t encoded_len;
static int encode_result;
static unsigned int captures;
static uint8_t original[1024];
static size_t original_len;

struct decoded_log {
	uint32_t uptime_ms;
	uint64_t address;
	struct zcbor_string args;
	struct zcbor_string body;
	struct zcbor_string body_template;
	struct zcbor_string strings[8];
	uint32_t offsets[8];
	size_t string_count;
	bool compact;
};

static void capture(const struct log_backend* backend, union log_msg_generic* msg)
{
	ARG_UNUSED(backend);
	k_free(encoded);
	encoded = NULL;
	const uint8_t* package = log_msg_get_package(&msg->log, &original_len);
	zassert_true(original_len <= sizeof(original));
	memcpy(original, package, original_len);
	/* Use a fixed sequence number to check that it is preserved in the payload. */
	encode_result = spotflow_cbor_encode_log(&msg->log, 42, &context, &encoded, &encoded_len);
	captures++;
}

static void panic_backend(const struct log_backend* backend)
{
	ARG_UNUSED(backend);
}
static const struct log_backend_api api = { .process = capture, .panic = panic_backend };
LOG_BACKEND_DEFINE(cbor_test, api, true);

static void before(void* fixture)
{
	ARG_UNUSED(fixture);
	while (log_process()) {
	}
	captures = 0;
	k_free(encoded);
	encoded = NULL;
}

static void expect_string(struct zcbor_string value, const char* expected)
{
	zassert_equal(value.len, strlen(expected));
	zassert_mem_equal(value.value, expected, value.len);
}

static struct decoded_log decode(void)
{
	while (log_process()) {
	}
	zassert_equal(captures, 1);
	zassert_ok(encode_result);
	ZCBOR_STATE_D(state, 4, encoded, encoded_len, 1, 0);
	struct decoded_log result = { 0 };
	zassert_true(zcbor_map_start_decode(state));
	while (!zcbor_array_at_end(state)) {
		uint32_t key;
		zassert_true(zcbor_uint32_decode(state, &key));
		switch (key) {
		case 0: /* messageType: 0 identifies a log message. */
			zassert_true(zcbor_uint32_expect(state, 0));
			break;
		case 4: /* severity: 40 corresponds to LOG_LEVEL_INF. */
			zassert_true(zcbor_uint32_expect(state, 40));
			break;
		case 13: /* sequenceNumber: 42 is supplied by capture(). */
			zassert_true(zcbor_uint32_expect(state, 42));
			break;
		case 1: /* body */
			zassert_true(zcbor_tstr_decode(state, &result.body));
			break;
		case 2: /* bodyTemplate */
			zassert_true(zcbor_tstr_decode(state, &result.body_template));
			break;
		case 46: /* compactBodyTemplate */
			result.compact = true;
			zassert_true(zcbor_uint64_decode(state, &result.address));
			break;
		case 47: /* compactBodyTemplateValues */
			zassert_true(zcbor_bstr_decode(state, &result.args));
			break;
		case 48: /* compactEmbeddedStrings */
			zassert_true(zcbor_map_start_decode(state));
			while (!zcbor_array_at_end(state)) {
				size_t i = result.string_count++;
				zassert_true(i < ARRAY_SIZE(result.strings));
				zassert_true(zcbor_uint32_decode(state, &result.offsets[i]));
				zassert_true(zcbor_tstr_decode(state, &result.strings[i]));
			}
			zassert_true(zcbor_map_end_decode(state));
			break;
		case 6: /* deviceUptimeMs */
			zassert_true(zcbor_uint32_decode(state, &result.uptime_ms));
			break;
		case 5: /* labels */
			zassert_true(zcbor_any_skip(state, NULL));
			break;
		default:
			zassert_unreachable("Unexpected CBOR key %u", key);
		}
	}
	zassert_true(zcbor_map_end_decode(state));
	zassert_equal(state->payload, encoded + encoded_len);
	if (result.compact) {
		struct cbprintf_package_hdr_ext hdr;
		memcpy(&hdr, original, sizeof(hdr));
		zassert_equal(result.address, (uintptr_t)hdr.fmt);
		zassert_equal(result.args.len, hdr.hdr.desc.len * sizeof(int) - sizeof(hdr));
		if (result.args.len) {
			zassert_mem_equal(result.args.value, original + sizeof(hdr),
					  result.args.len);
		}
		zassert_is_null(result.body.value);
		zassert_is_null(result.body_template.value);
	}
	return result;
}

ZTEST(log_cbor, test_timestamp)
{
	/* Deferred processing must preserve the time of message creation. */
	k_sleep(K_MSEC(100));
	uint32_t start = k_uptime_get_32();
	LOG_INF("timestamp");
	uint32_t end = k_uptime_get_32();
	k_sleep(K_MSEC(100));
	struct decoded_log result = decode();
	zassert_true(result.uptime_ms >= start);
	zassert_true(result.uptime_ms <= end);
}

ZTEST(log_cbor, test_no_arguments)
{
	LOG_INF("literal %%");
	struct decoded_log result = decode();
	zassert_equal(result.compact, IS_ENABLED(CONFIG_TEST_COMPACT_LOGS));
	zassert_equal(result.args.len, 0);
	zassert_equal(result.string_count, 0);
	if (!result.compact) {
		expect_string(result.body, "literal %");
		expect_string(result.body_template, "literal %%");
	}
}

ZTEST(log_cbor, test_mixed_arguments_and_padding)
{
	LOG_INF("mixed %d %lld %f %s %p", -17, 0x1122334455667788LL, 1.5, "constant",
		(void*)(uintptr_t)0x1234);
	struct decoded_log result = decode();
	zassert_equal(result.compact, IS_ENABLED(CONFIG_TEST_COMPACT_LOGS));
	/* Zephyr's native POSIX target cannot classify rodata addresses, so cbprintf
	 * copies even the "constant" string argument into the package.
	 */
	zassert_equal(result.string_count, result.compact && IS_ENABLED(CONFIG_ARCH_POSIX) ? 1 : 0);
}

ZTEST(log_cbor, test_transient_strings)
{
	char first[] = "transient";
	char second[] = "";
	LOG_INF("strings %s %u %s %s", first, 23u, "constant", second);
	memset(first, 'X', sizeof(first) - 1);
	struct decoded_log result = decode();
	if (result.compact) {
		/* POSIX also embeds "constant", in addition to the two transient strings. */
		zassert_equal(result.string_count, IS_ENABLED(CONFIG_ARCH_POSIX) ? 3 : 2);
		expect_string(result.strings[0], "transient");
		expect_string(result.strings[result.string_count - 1], "");
		size_t offset =
			ROUND_UP(sizeof(struct cbprintf_package_hdr_ext), VA_STACK_ALIGN(char*));
		zassert_equal(result.offsets[0], offset - sizeof(struct cbprintf_package_hdr_ext));
		offset += sizeof(char*);
		offset = ROUND_UP(offset, VA_STACK_ALIGN(unsigned int)) + sizeof(unsigned int);
		offset = ROUND_UP(offset, VA_STACK_ALIGN(char*)) + sizeof(char*);
		offset = ROUND_UP(offset, VA_STACK_ALIGN(char*));
		zassert_equal(result.offsets[result.string_count - 1],
			      offset - sizeof(struct cbprintf_package_hdr_ext));
	} else {
		expect_string(result.body, "strings transient 23 constant ");
	}
}

ZTEST(log_cbor, test_large_formatted_output)
{
	LOG_INF("%200u", 7u);
	while (log_process()) {
	}
	if (IS_ENABLED(CONFIG_TEST_COMPACT_LOGS)) {
		zassert_true(decode().compact);
	} else {
		zassert_equal(encode_result, -ENOMEM);
	}
}

/* Exercise runtime argument packaging in addition to the LOG_INF macros. */
static void runtime_log(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	log_generic(LOG_LEVEL_INF, fmt, ap);
	va_end(ap);
}

/* Zephyr logging treats the format string as read-only, including in runtime
 * packaging. Use its packager for arguments, then append the format ourselves
 * to exercise packages that carry the format as an embedded string.
 */
static void embedded_format_log(const char* format, ...)
{
	uint8_t storage[sizeof(struct log_msg) + 256] __aligned(Z_LOG_MSG_ALIGNMENT) = { 0 };
	struct log_msg* msg = (void*)storage;
	va_list ap;
	va_start(ap, format);
	int len = cbvprintf_package(msg->data, 256, 0, format, ap);
	va_end(ap);
	zassert_true(len > 0);

	struct cbprintf_package_hdr_ext* hdr = (void*)msg->data;
	size_t format_len = strlen(format) + 1;
	zassert_true((size_t)len + 1 + format_len <= sizeof(storage) - sizeof(*msg));
	/* Keep any embedded argument strings before the format entry. */
	hdr->hdr.desc.str_cnt++;
	msg->data[len++] = offsetof(struct cbprintf_package_hdr_ext, fmt) / sizeof(int);
	memcpy(msg->data + len, format, format_len);
	msg->hdr.desc.package_len = len + format_len;
	msg->hdr.desc.level = LOG_LEVEL_INF;
	capture(NULL, (union log_msg_generic*)msg);
}

ZTEST(log_cbor, test_embedded_format)
{
	embedded_format_log("runtime %u", 19u);
	if (IS_ENABLED(CONFIG_TEST_COMPACT_LOGS)) {
		zassert_equal(encode_result, -EINVAL);
		zassert_is_null(encoded);
	} else {
		struct decoded_log result = decode();
		expect_string(result.body_template, "runtime %u");
		expect_string(result.body, "runtime 19");
	}
}

#ifdef CONFIG_TEST_COMPACT_LOGS
ZTEST(log_cbor, test_embedded_format_with_transient_argument)
{
	char text[] = "transient";
	embedded_format_log("%200u %s", 19u, text);
	zassert_equal(encode_result, -EINVAL);
	zassert_is_null(encoded);
}

ZTEST(log_cbor, test_embedded_format_without_arguments)
{
	embedded_format_log("literal %%");
	zassert_equal(encode_result, -EINVAL);
	zassert_is_null(encoded);
}
#endif /* CONFIG_TEST_COMPACT_LOGS */

ZTEST(log_cbor, test_runtime_packaging)
{
	char text[] = "runtime string";
	runtime_log("runtime %d %s %f", -9, text, 2.5);
	struct decoded_log result = decode();
	zassert_equal(result.compact, IS_ENABLED(CONFIG_TEST_COMPACT_LOGS));
	if (result.compact) {
		zassert_equal(result.string_count, 1);
		expect_string(result.strings[0], "runtime string");
	}
}

ZTEST(log_cbor, test_cbor_buffer_overflow)
{
	char text[600];
	memset(text, 'A', sizeof(text) - 1);
	text[sizeof(text) - 1] = '\0';
	LOG_INF("%s", text);
	while (log_process()) {
	}
	zassert_equal(captures, 1);
	zassert_true(encode_result < 0);
	zassert_is_null(encoded);
}

#ifdef CONFIG_TEST_COMPACT_LOGS
static int encode_package_for_test(const uint8_t* package, size_t size)
{
	uint8_t storage[sizeof(struct log_msg) + 128] __aligned(Z_LOG_MSG_ALIGNMENT) = { 0 };
	struct log_msg* msg = (struct log_msg*)storage;
	msg->hdr.desc.package_len = size;
	msg->hdr.desc.level = LOG_LEVEL_INF;
	memcpy(msg->data, package, size);
	uint8_t* result = NULL;
	size_t result_len = 0;
	int rc = spotflow_cbor_encode_log(msg, 42, &context, &result, &result_len);
	k_free(result);
	return rc;
}

ZTEST(log_cbor, test_invalid_packages)
{
	uint8_t package[128] __aligned(CBPRINTF_PACKAGE_ALIGNMENT) = { 0 };
	struct cbprintf_package_hdr_ext* hdr = (void*)package;
	const size_t prefix = sizeof(*hdr);
	hdr->fmt = "value %s";
	hdr->hdr.desc.len = (prefix + sizeof(char*)) / sizeof(int);
	const size_t args_end = hdr->hdr.desc.len * sizeof(int);

	zassert_equal(encode_package_for_test(package, prefix - 1), -EINVAL);
	zassert_equal(encode_package_for_test(package, args_end - 1), -EINVAL);
	hdr->hdr.desc.ro_str_cnt = 1;
	zassert_equal(encode_package_for_test(package, args_end), -EINVAL);
	hdr->hdr.desc.ro_str_cnt = 0;
	hdr->hdr.desc.rw_str_cnt = 1;
	zassert_equal(encode_package_for_test(package, args_end), -EINVAL);
	hdr->hdr.desc.rw_str_cnt = 0;
	hdr->hdr.desc.str_cnt = 1;
	zassert_equal(encode_package_for_test(package, args_end), -EINVAL);
	package[args_end] = prefix / sizeof(int);
	package[args_end + 1] = 'X';
	zassert_equal(encode_package_for_test(package, args_end + 2), -EINVAL);
	package[args_end + 1] = 0;
	package[args_end] = 0; /* Header slot, not an argument or format pointer. */
	zassert_equal(encode_package_for_test(package, args_end + 2), -EINVAL);
	package[args_end] = args_end / sizeof(int); /* Beyond argument bytes. */
	zassert_equal(encode_package_for_test(package, args_end + 2), -EINVAL);
	package[args_end] = prefix / sizeof(int);
	package[args_end + 2] = prefix / sizeof(int);
	package[args_end + 3] = 0;
	hdr->hdr.desc.str_cnt = 2;
	zassert_equal(encode_package_for_test(package, args_end + 4), -EINVAL);
}

ZTEST(log_cbor, test_non_dereferenceable_format_address)
{
	uint8_t package[sizeof(struct cbprintf_package_hdr_ext)] = { 0 };
	struct cbprintf_package_hdr_ext hdr = { 0 };
	hdr.fmt = (char*)(uintptr_t)0x12345678;
	hdr.hdr.desc.len = sizeof(hdr) / sizeof(int);
	memcpy(package, &hdr, sizeof(hdr));
	zassert_ok(encode_package_for_test(package, sizeof(package)));
}
#endif /* CONFIG_TEST_COMPACT_LOGS */

ZTEST_SUITE(log_cbor, NULL, NULL, before, NULL, NULL);
