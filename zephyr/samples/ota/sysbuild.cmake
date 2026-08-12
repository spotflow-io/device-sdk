# SPDX-License-Identifier: Apache-2.0

# This board has no MCUboot partitions in its upstream DTS. MCUboot also needs
# a private working-RAM region while it copies and verifies the application.
if(BOARD MATCHES "^cy8cproto_062_4343w")
	list(APPEND mcuboot_EXTRA_CONF_FILE
		"${APP_DIR}/sysbuild/mcuboot_cy8cproto_062_4343w.conf"
	)
	set(mcuboot_EXTRA_CONF_FILE
		"${mcuboot_EXTRA_CONF_FILE}"
		CACHE INTERNAL "MCUboot configuration for cy8cproto_062_4343w"
	)
	set(mcuboot_EXTRA_DTC_OVERLAY_FILE
		"${APP_DIR}/boards/cy8cproto_062_4343w_mcuboot.overlay"
		CACHE INTERNAL "MCUboot overlay for cy8cproto_062_4343w"
	)
endif()
