#ifndef SPOTFLOW_SHOWCASE_H
#define SPOTFLOW_SHOWCASE_H

#include <stdbool.h>

#include <zephyr/kernel.h>

enum showcase_button_action {
	SHOWCASE_BUTTON_SHORT_PRESS,
	SHOWCASE_BUTTON_LONG_PRESS,
};

int showcase_button_init(void);
int showcase_button_get_action(enum showcase_button_action* action, k_timeout_t timeout);

int showcase_ota_init(void);
void showcase_ota_handle_short_press(void);
bool showcase_ota_handle_long_press(void);

int showcase_telemetry_init(void);
void showcase_telemetry_emit(void);

#endif /* SPOTFLOW_SHOWCASE_H */
