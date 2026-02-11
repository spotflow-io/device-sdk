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

static struct my_context context;

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
	context.value = arg;
}
```

## Header File Structure

Use include guards, not `#pragma once`. Wrap declarations in `extern "C"` for
C++ compatibility.

```c
#ifndef SPOTFLOW_EXAMPLE_H
#define SPOTFLOW_EXAMPLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* declarations */

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_EXAMPLE_H */
```

## Naming Conventions

- **Public functions**: `spotflow_<module>_<action>` (e.g., `spotflow_metrics_system_init`)
- **Static functions**: descriptive names without prefix (e.g., `report_thread_stack`)
- **Public macros/constants**: `SPOTFLOW_` prefix, `UPPER_SNAKE_CASE`
- **Kconfig options**: `CONFIG_SPOTFLOW_*`
- **Structs**: `struct spotflow_<name>` (no typedef)
- **File names**: `spotflow_<subsystem>[_<component>].c/.h`

## Logging

- Register per-file: `LOG_MODULE_REGISTER(spotflow_<name>, CONFIG_SPOTFLOW_*_LOG_LEVEL);`
- Or declare: `LOG_MODULE_DECLARE(spotflow_<name>);`
- Use `LOG_ERR`, `LOG_WRN`, `LOG_INF`, `LOG_DBG` -- never `printk` in library code

## Error Handling

- Return negative errno values: `-EINVAL`, `-ENOMEM`, `-ENOSPC`, `-ENOBUFS`
- Validate all pointer arguments against `NULL` at function entry
- Log errors with `LOG_ERR` before returning an error code
- Log warnings with `LOG_WRN` for non-fatal issues (e.g., truncation)
- Document return codes in Doxygen `@return` blocks on public API headers

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
int spotflow_report_metric_int(struct spotflow_metric_int* metric, int64_t value);
```

## Conditional Compilation

- Guard subsystem includes and code with `#ifdef CONFIG_SPOTFLOW_*`
- End guards with a comment: `#endif /* CONFIG_SPOTFLOW_* */`
- Use `zephyr_library_sources_ifdef()` in CMake, not raw `if()`

## Kconfig Conventions

- New options go under the `if SPOTFLOW` block in the relevant `Kconfig` file
- Use `depends on` for hard dependencies, `select` for auto-enabled deps
- Provide `help` text for all user-facing options
- Use `configdefault` (not `config ... default`) when overriding Zephyr defaults
