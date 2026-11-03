#ifndef VERSION_HELPER_H
#define VERSION_HELPER_H

#if __has_include(<zephyr/version.h>)
#include <zephyr/version.h>
#else
#include <version.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_ZEPHYR_VERSION_GE(major, minor) \
	((KERNEL_VERSION_MAJOR > (major)) ||     \
	 (KERNEL_VERSION_MAJOR == (major) && KERNEL_VERSION_MINOR >= (minor)))

#ifdef __cplusplus
}
#endif

#endif //VERSION_HELPER_H
