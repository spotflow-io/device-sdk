#ifndef SPOTFLOW_LOG_SOURCE_COMPAT_H
#define SPOTFLOW_LOG_SOURCE_COMPAT_H

#include <zephyr/logging/log.h>

/* Zephyr 3.7 sets log_const.name to NULL with LOG_FMT_SECTION_STRIP, losing the
 * address required by compact labels. This header is force-included only for
 * stripped compact builds on 3.7. Preserve the pointer with the registration
 * macro used in Zephyr 4.1 and later; the string stays in the ELF-only section.
 */
#undef _LOG_MODULE_CONST_DATA_CREATE
#define _LOG_MODULE_CONST_DATA_CREATE(_name, _level)                                             \
	IF_ENABLED(CONFIG_LOG_FMT_SECTION,                                                       \
		   (static const char UTIL_CAT(_name, _str)[] __in_section(_log_strings, static, \
									   _CONCAT(_name, _))    \
			    __used __noasan = STRINGIFY(_name);))                                \
	IF_ENABLED(LOG_IN_CPLUSPLUS, (extern))                                                   \
	const STRUCT_SECTION_ITERABLE_ALTERNATE(log_const, log_source_const_data,                \
						Z_LOG_ITEM_CONST_DATA(_name)) = {                \
		.name = COND_CODE_1(CONFIG_LOG_FMT_SECTION, (UTIL_CAT(_name, _str)),             \
				    (STRINGIFY(_name))),                                         \
		.level = (_level),                                                               \
	}

#endif /* SPOTFLOW_LOG_SOURCE_COMPAT_H */
