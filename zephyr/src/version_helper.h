#ifndef VERSION_HELPER_H
#define VERSION_HELPER_H

#include <version.h>

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
