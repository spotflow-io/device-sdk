/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef METRICS_SPOTFLOW_RESET_HELPER_H
#define METRICS_SPOTFLOW_RESET_HELPER_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
void report_reboot_reason(void);

#ifdef __cplusplus
}
#endif

#endif //METRICS_SPOTFLOW_RESET_HELPER_H
