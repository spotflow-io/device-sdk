#include <zephyr/bindesc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_stats.h>
#include <zephyr/sys/mem_stats.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/kernel/thread_stack.h>

#include "../../common/wifi.h"


LOG_MODULE_REGISTER(SPOTF_MAIN, LOG_LEVEL_DBG);

/* Uncomment this function to provide your own device ID in runtime */
/*const char* spotflow_override_device_id()
{
	return "my_nrf7002dk_test";
}*/

static void capture_network_stats()
{
	uint32_t ipv4_recv;
	uint32_t ipv4_sent;
	uint32_t ipv4_drop;
	struct net_if* iface = net_if_get_default();
	if (iface) {
		const struct net_stats_ip *ip = &iface->stats.ipv4;
		ipv4_recv = ip->recv;
		ipv4_sent = ip->sent;
		ipv4_drop = ip->drop;
		LOG_DBG("Network: ipv4_recv=%u ipv4_sent=%u ipv4_drop=%u", ipv4_recv, ipv4_sent,
			ipv4_drop);
	}
}

// extern struct sys_heap z_heap_runtime_stats;
extern struct sys_heap _system_heap; // declared by Zephyr if k_malloc() is used

static void capture_heap_stats()
{
	struct sys_memory_stats stats;

	if (sys_heap_runtime_stats_get(&_system_heap, &stats) == 0) {
		LOG_DBG("Heap free: %zu bytes", stats.free_bytes);
		LOG_DBG("Heap allocated: %zu bytes", stats.allocated_bytes);
	}

}

static void thread_info_cb(const struct k_thread *thread, void *user_data)
{
	ARG_UNUSED(user_data);

	// struct k_thread_stack_info info;

	size_t unused, size = thread->stack_info.size;
	if (k_thread_stack_space_get(thread, &unused) == 0) {
		size_t used = size - unused;
		const char *tname;

		tname = k_thread_name_get((k_tid_t)thread);

		if (tname == NULL) {
			static char buf[32];
			snprintk(buf, sizeof(buf), "%p", (void *)thread);
			tname = buf;
		}
		// LOG_INF("%p (%s):\tunused %zu\tusage %zu / %zu (%u %%)",
		// 	thread, tname, unused, size - unused, size,
		// 	pcnt);

		// unsigned int pcnt = ((size - unused) * 100U) / size;
		LOG_DBG("Thread: %s, Stack size: %zu, Used: %zu", tname, size, used);
		// LOG_DBG("  ", used);
		// LOG_DBG("  Unused:     %zu\n", unused);
	}
}

static void capture_threads_stacks()
{
	k_thread_foreach(thread_info_cb, NULL);

}

int main(void)
{
	LOG_INF("Starting Spotflow logging example");

	// Wait for the initialization of Wi-Fi device
	k_sleep(K_SECONDS(1));

	init_wifi();
	connect_to_wifi();

	while (1) {
		capture_network_stats();
		capture_threads_stacks();
		capture_heap_stats();
		// metrics_capture(&s);
		// metrics_log(&s);
		k_sleep(K_SECONDS(5));
	}
	return 0;
}
