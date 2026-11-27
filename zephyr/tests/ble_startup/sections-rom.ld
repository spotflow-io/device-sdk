#include <zephyr/linker/iterable_sections.h>

/* These tests stub the Bluetooth host, which normally supplies this section.
 * Keep the table registered by BT_CONN_CB_DEFINE() so the tests can find it
 * with STRUCT_SECTION_START(bt_conn_cb) and call the connection callbacks.
 */
ITERABLE_SECTION_ROM(bt_conn_cb, Z_LINK_ITERABLE_SUBALIGN)
