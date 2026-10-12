#include <zephyr/sys/cbprintf.h>
#include <zephyr/sys/util.h>

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>

#define TYPE_PAIR(type) sizeof(type), VA_STACK_ALIGN(type)

/* cbprintf advances its cursor in four-byte units. Static and runtime
 * packaging must agree on alignment for this single layout to describe both.
 */
#define CHECK_TYPE(type)                                                      \
	BUILD_ASSERT(sizeof(type) == 4 || sizeof(type) == 8);                 \
	BUILD_ASSERT(VA_STACK_ALIGN(type) >= 1 && VA_STACK_ALIGN(type) <= 8); \
	BUILD_ASSERT(CBPRINTF_PACKAGE_ALIGNMENT % VA_STACK_ALIGN(type) == 0); \
	BUILD_ASSERT(MAX(4, VA_STACK_ALIGN(type)) == MAX(4, Z_CBPRINTF_ALIGNMENT((type)0)))

/* Reject targets whose integer representations or double format do not match
 * the cloud decoder's semantics profile (two's complement and IEEE binary64).
 */
BUILD_ASSERT(CHAR_BIT == 8 && sizeof(short) == 2 && SHRT_MAX == 32767);
BUILD_ASSERT(sizeof(int) == 4 && INT_MAX == INT32_MAX && INT_MIN == -INT_MAX - 1);
BUILD_ASSERT(sizeof(long long) == 8 && LLONG_MAX == INT64_MAX && LLONG_MIN == -LLONG_MAX - 1);
BUILD_ASSERT(LONG_MIN == -LONG_MAX - 1 && SHRT_MIN == -SHRT_MAX - 1);
BUILD_ASSERT(sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024 &&
	     DBL_MIN_EXP == -1021);

/* The layout describes untagged arguments. */
BUILD_ASSERT(!IS_ENABLED(CONFIG_CBPRINTF_PACKAGE_SUPPORT_TAGGED_ARGUMENTS));

/* The omitted prefix must fit in one metadata byte, preserve cbprintf's
 * four-byte cursor granularity, and end immediately after the format pointer.
 */
BUILD_ASSERT(sizeof(struct cbprintf_package_hdr_ext) <= UINT8_MAX);
BUILD_ASSERT(sizeof(struct cbprintf_package_hdr_ext) % 4 == 0);
BUILD_ASSERT(offsetof(struct cbprintf_package_hdr_ext, fmt) + sizeof(char*) ==
	     sizeof(struct cbprintf_package_hdr_ext));

CHECK_TYPE(int);
CHECK_TYPE(long);
CHECK_TYPE(long long);
CHECK_TYPE(intmax_t);
CHECK_TYPE(size_t);
CHECK_TYPE(ptrdiff_t);
CHECK_TYPE(void*);
CHECK_TYPE(double);

/* Schema 1, semantics profile 1. The argument payload starts after the
 * cbprintf header's format pointer and retains leading and internal padding.
 * All fields are bytes, so the record is independent of target byte order.
 */
static const uint8_t profile[24] __used __attribute__((section(".spotflow.compact_logs"))) = {
	'S',
	'C',
	'L',
	'G',
	1,
	1,
	sizeof(struct cbprintf_package_hdr_ext),
	8,
	TYPE_PAIR(int),
	TYPE_PAIR(long),
	TYPE_PAIR(long long),
	TYPE_PAIR(intmax_t),
	TYPE_PAIR(size_t),
	TYPE_PAIR(ptrdiff_t),
	TYPE_PAIR(void*),
	TYPE_PAIR(double),
};
