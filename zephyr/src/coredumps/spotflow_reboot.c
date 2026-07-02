#include "zephyr/app_memory/app_memdomain.h"
#include "zephyr/logging/log.h"
#include "zephyr/logging/log_ctrl.h"
#include "zephyr/sys/reboot.h"

LOG_MODULE_DECLARE(spotflow_coredump, LOG_LEVEL_ERR);

/*to reboot the device after panic*/
FUNC_NORETURN void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf* esf)
{
	ARG_UNUSED(esf);
	LOG_PANIC();
	LOG_ERR("Halting system");
#ifdef CONFIG_DEBUG_COREDUMP_BACKEND_IN_MEMORY
	sys_reboot(SYS_REBOOT_WARM);
#else
	sys_reboot(SYS_REBOOT_COLD);
#endif
	CODE_UNREACHABLE;
}
