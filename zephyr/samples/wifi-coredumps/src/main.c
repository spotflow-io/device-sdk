#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "wifi.h"
#include "zephyr/debug/coredump.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_INF);

void dump_coredump()
{
	/* … */
	unsigned int reason = K_ERR_KERNEL_OOPS; /* or K_ERR_KERNEL_PANIC, etc. */
	const struct arch_esf* esf = NULL; /* no exception context */
	struct k_thread* thr = k_current_get(); /* dump the current thread */

	coredump(reason, esf, thr);
	/* Note: after this the dump will be emitted but your code keeps running */
}

int main(void)
{
	LOG_INF("Starting Spotflow logging example");

	// Wait for the initialization of Wi-Fi device
	k_sleep(K_SECONDS(1));

	init_wifi();
	connect_to_wifi();
	LOG_INF("going to dump coredump");
	dump_coredump();
	for (int i = 0; i < 20; i++) {
		LOG_INF("Hello from Zephyr to Spotflow: %d", i);
		k_sleep(K_SECONDS(2));
	}

	LOG_INF("Going to crash the system");
	k_panic();

	return 0;
}
