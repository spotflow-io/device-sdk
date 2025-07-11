#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "wifi.h"
#include "zephyr/debug/coredump.h"

#include <zephyr/sys/reboot.h>
#include <zephyr/fatal.h>   /* for k_sys_fatal_error_handler */
#include <zephyr/logging/log_ctrl.h>

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_INF);


int main(void)
{
	LOG_INF("Starting Spotflow logging example");

	// Wait for the initialization of Wi-Fi device
	k_sleep(K_SECONDS(1));

	init_wifi();
	connect_to_wifi();
	for (int i = 0; i < 20; i++) {
		LOG_INF("Hello from Zephyr to Spotflow: %d", i);
		k_sleep(K_SECONDS(2));
		if (i == 5) {
			LOG_ERR("Simulating a crash at iteration %d", i);
			k_panic();
			// Simulate a crash to trigger coredump
		}
	}

	LOG_INF("Going to crash the system");


	return 0;
}

FUNC_NORETURN void k_sys_fatal_error_handler(unsigned int reason,
				      const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	LOG_ERR("Halting system");
	sys_reboot(SYS_REBOOT_COLD);
	CODE_UNREACHABLE;
}
