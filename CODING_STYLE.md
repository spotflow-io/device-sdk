# Coding Style Guide

This document describes the coding conventions used in the Spotflow SDK.

## Formatting

Code formatting is enforced by `.clang-format`. Run `clang-format` before committing.

## C Source File Structure

Source files should follow this order:

1. **Copyright header**
2. **Includes**
   - Own header first (e.g., `#include "my_module.h"`)
   - Project headers (e.g., `#include "metrics/spotflow_metrics_backend.h"`)
   - Zephyr/system headers (e.g., `#include <zephyr/kernel.h>`)
3. **LOG_MODULE_REGISTER / LOG_MODULE_DECLARE**
4. **Defines and macros**
5. **Type definitions** (structs, enums, typedefs)
6. **Static variables**
7. **Forward declarations of static functions**
8. **Public function implementations**
9. **Static function implementations**

### Example

```c

#include "my_module.h"
#include "other/project_header.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(my_module, CONFIG_MY_MODULE_LOG_LEVEL);

#define MY_CONSTANT 42

struct my_context {
	int value;
};

static struct my_context g_context;

static void helper_function(int arg);

int my_module_init(void)
{
	/* Public function implementation */
	helper_function(MY_CONSTANT);
	return 0;
}

static void helper_function(int arg)
{
	/* Static function implementation */
	g_context.value = arg;
}
```

## Naming Conventions

- **Functions**: `spotflow_<module>_<action>` (e.g., `spotflow_metrics_system_init`)
- **Static functions**: descriptive names without prefix (e.g., `report_thread_stack`)
- **Constants/Macros**: `UPPER_SNAKE_CASE`

## Documentation Comments

Use Doxygen comments to document all public API functions and types in header files.

- Use `/** ... */` style for Doxygen comments
- Use `@brief` for a one-line summary
- Use `@param` for each parameter
- Use `@return` for return values (include error codes where applicable)
- Use `@note` for important usage notes
- Static/internal functions do not require Doxygen comments (regular `/* */` comments suffice)

### Example

```c
/**
 * @brief Report a label-less integer metric value
 *
 * @param metric Metric handle from registration
 * @param value Integer value to report
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: Invalid metric handle
 *         -EAGAIN: Aggregator busy (rare, retry)
 */
int spotflow_report_metric_int(struct spotflow_metric_int *metric, int64_t value);
```
