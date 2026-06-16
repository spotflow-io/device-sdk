# Spotflow BLE Logs Transport Plan

## Goal

Add support for sending Spotflow logs from a BLE peripheral to a BLE central/gateway.

The peripheral will expose the Spotflow GATT service described in `BLE Design POC.md`. The
gateway will read device/session metadata and relay log telemetry received over BLE to Spotflow
MQTT topics.

Initial scope:

- Logs only
- BLE peripheral side only
- No ACK/NACK support
- No metrics
- No coredumps
- No cloud-to-device configuration handling in the first implementation

## Current Log Pipeline

Current MQTT path:

```text
Zephyr log backend
-> Spotflow CBOR log encoder
-> g_spotflow_logs_msgq
-> spotflow_log_net.c
-> spotflow_mqtt_publish_ingest_cbor_msg()
-> MQTT ingest-cbor
```

Desired BLE path:

```text
Zephyr log backend
-> Spotflow CBOR log encoder
-> g_spotflow_logs_msgq
-> BLE TX Stream TELEMETRY frames
-> BLE gateway
-> MQTT ingest-cbor
```

The BLE path should reuse the existing Spotflow CBOR log payloads unchanged.

## Design Principles

- Keep log CBOR format unchanged.
- Keep the existing log backend and log queue behavior.
- Add BLE as a transport below the existing queue.
- Do not make the BLE sample depend on MQTT credentials.
- Do not implement ACK/NACK in the first version.
- Return `-EAGAIN` when BLE is temporarily unavailable so queued logs are retried later.

## Library Changes

### 1. Add Transport Selection

Introduce a Kconfig transport choice:

```kconfig
choice SPOTFLOW_TRANSPORT
	default SPOTFLOW_TRANSPORT_MQTT

config SPOTFLOW_TRANSPORT_MQTT
	bool "MQTT/TLS direct transport"

config SPOTFLOW_TRANSPORT_BLE
	bool "BLE gateway transport"

endchoice
```

MQTT-only dependencies should be selected only when `SPOTFLOW_TRANSPORT_MQTT` is enabled.

BLE transport should not require:

- `CONFIG_SPOTFLOW_INGEST_KEY`
- MQTT library
- TLS socket support
- DNS/networking stack

### 2. Add Internal Transport API

Add an internal abstraction used by log processing:

```c
int spotflow_transport_start(void);
bool spotflow_transport_is_ready(void);
int spotflow_transport_send_ingest_cbor(uint8_t *payload, size_t len);
```

For the initial BLE logs implementation, only ingest CBOR sending is required.

MQTT implementation:

```text
spotflow_transport_send_ingest_cbor()
-> spotflow_mqtt_publish_ingest_cbor_msg()
```

BLE implementation:

```text
spotflow_transport_send_ingest_cbor()
-> fragment as TELEMETRY
-> send over TX Stream notifications
```

### 3. Update Log Network Processing

Change `spotflow_log_net.c` to call:

```c
spotflow_transport_send_ingest_cbor(msg_ptr->payload, msg_ptr->len);
```

instead of:

```c
spotflow_mqtt_publish_ingest_cbor_msg(msg_ptr->payload, msg_ptr->len);
```

Queue semantics should stay unchanged:

- Peek first.
- Send while message remains in queue.
- Remove and free only after successful full send.
- Keep queued on `-EAGAIN`.

### 4. Session Metadata Encoding API

The BLE POC requires `Session Metadata` as a readable GATT characteristic.

Currently session metadata is encoded and immediately published via MQTT.

Add a reusable encoder API:

```c
int spotflow_session_metadata_encode(uint8_t *buffer, size_t buffer_len, size_t *encoded_len);
```

MQTT can continue using a wrapper that publishes the encoded buffer.

BLE service will use this encoder in the `Session Metadata` read handler.

### 5. Device ID Characteristic

Reuse existing API:

```c
const char *spotflow_get_device_id(void);
```

The BLE `Device Id` characteristic should expose this value as a read characteristic.

## BLE GATT Service

Implement the Spotflow BLE service from `BLE Design POC.md`.

Base UUID:

```text
2653xxxx-81E5-4861-82AE-2C92E6887922
```

Characteristics:

| Name | UUID | Properties | Initial Scope |
| --- | --- | --- | --- |
| Capabilities | `26530002-81E5-4861-82AE-2C92E6887922` | READ | Return protocol version |
| Device Id | `26530003-81E5-4861-82AE-2C92E6887922` | READ | Use `spotflow_get_device_id()` |
| Session Metadata | `26530004-81E5-4861-82AE-2C92E6887922` | READ | Use encoded session metadata |
| TX Stream | `26530005-81E5-4861-82AE-2C92E6887922` | NOTIFY | Send log telemetry |
| RX Stream | `26530006-81E5-4861-82AE-2C92E6887922` | WRITE WITHOUT RESPONSE | Stub or ignore initially |

## BLE Framing

Logs are sent as `TELEMETRY` messages.

Message type:

```text
TELEMETRY = 0x02
```

First fragment:

```text
byte 0: message type, 0x02
byte 1: flags
byte 2: sequence number
byte 3: total message length low byte
byte 4: total message length high byte
byte 5+: CBOR fragment
```

Continuation fragment:

```text
byte 0: message type, 0x02
byte 1: flags
byte 2: sequence number
byte 3+: CBOR fragment
```

Flags:

```text
IS_FIRST = BIT(0)
IS_LAST  = BIT(1)
```

For the first version:

```text
NEEDS_ACK is always unset.
ACK/NACK messages are not implemented.
```

## BLE Send Behavior

`spotflow_transport_send_ingest_cbor()` should:

1. Check that a central is connected.
2. Check that notifications are enabled on TX Stream.
3. Determine available notification payload size from ATT MTU.
4. Fragment the CBOR payload.
5. Send all fragments in order.
6. Return success only when all fragments were accepted for transmission.
7. Return `-EAGAIN` if BLE is disconnected, notifications are disabled, or buffers are temporarily unavailable.
8. Return a negative fatal error only for unrecoverable failures.

The log queue item must be removed only after all fragments are sent successfully.

## Production Threading Model

The BLE transport must not rely on unsynchronized globals shared between Bluetooth callbacks and
Spotflow queue processing. Treat these contexts as concurrent:

- Bluetooth connection callbacks
- GATT CCC callbacks
- Advertising restart work
- Spotflow processor thread calling `spotflow_transport_send_ingest_cbor()`

BLE transport state should be owned by one internal module, for example `spotflow_ble_transport.c`:

```c
struct spotflow_ble_transport_state {
    struct k_mutex lock;
    struct bt_conn *conn;
    bool tx_notifications_enabled;
    uint8_t telemetry_sequence;
    struct k_work_delayable restart_advertising_work;
};
```

State access rules:

- Hold `lock` whenever reading or writing `conn`, `tx_notifications_enabled`, or
  `telemetry_sequence`.
- Callback handlers may update state and schedule work, but should not perform long sends.
- `spotflow_transport_send_ingest_cbor()` should take a local `bt_conn_ref()` while holding the
  lock, then release the lock before calling `bt_gatt_notify()`.
- Always `bt_conn_unref()` the local reference after all fragments are sent or after the first
  error.
- Return `-EAGAIN` for disconnected, unsubscribed, controller-buffer pressure, or other transient
  BLE send failures.
- Do not remove the log queue item until every fragment for that CBOR payload was accepted by
  `bt_gatt_notify()`.

The preferred send path is synchronous from the Spotflow processor thread:

```text
spotflow_poll_and_process_enqueued_logs()
-> spotflow_transport_send_ingest_cbor()
-> lock, copy/ref connection state, increment sequence
-> unlock
-> fragment and bt_gatt_notify()
-> unref connection
```

This keeps backpressure simple: if BLE is not ready or cannot currently accept notifications, the
existing log queue keeps the item and the processor retries later. A dedicated BLE send thread can be
added later for ACK/NACK windows, but it is not necessary for the first production logs transport.

Connection lifecycle rules:

- On connect, replace any previous connection reference under the lock.
- On disconnect, clear notification state and connection under the lock, then schedule advertising
  restart via delayable work.
- Advertising restart work should check connection state under the lock before calling
  `bt_le_adv_start()`.
- CCC changes should only update the notification flag under the lock.

The standalone BLE sample should mirror these rules, but the SDK implementation should keep the
logic in a reusable BLE transport module instead of in application `main.c`.

## Processing Thread

For MQTT transport, preserve current behavior:

```text
wait for network
initialize TLS
connect MQTT
send session metadata
process queues
```

For BLE transport:

```text
initialize BLE service
advertise Spotflow service UUID
wait for central connection and TX notifications enabled
process log queue
send logs as TELEMETRY notifications
```

The first BLE version does not need MQTT connection management, TLS, config subscription, metrics, or
coredump processing.

## Sample Plan

Sample location:

```text
modules/lib/spotflow/zephyr/samples/ble
```

Use Zephyr `samples/bluetooth/peripheral` as the conceptual base, because it already demonstrates:

- Custom GATT service
- Read characteristics
- Write without response
- Notifications/CCCD
- MTU callback
- Advertising custom UUIDs

Do not base the sample on `peripheral_nus`, because NUS uses a different service and characteristic
contract.

Sample behavior:

- Enable Bluetooth.
- Register/enable Spotflow BLE transport.
- Advertise Spotflow service UUID.
- Generate periodic Zephyr logs.
- Spotflow log backend encodes logs to CBOR.
- BLE transport streams them through TX Stream.

## Initial Implementation Order

1. Add transport Kconfig choice.
2. Split MQTT-specific dependencies behind `SPOTFLOW_TRANSPORT_MQTT`.
3. Add internal transport API.
4. Implement MQTT transport wrapper preserving current behavior.
5. Change log network processing to use transport API.
6. Add reusable session metadata encoder.
7. Add BLE service and BLE transport skeleton.
8. Implement TX Stream notification state tracking.
9. Implement TELEMETRY fragmentation.
10. Add BLE sample under `samples/ble`.
11. Build the BLE sample.
12. Verify logs are framed and notified to a BLE central.

## Deferred Work

- ACK/NACK support
- Desired/reported configuration over RX/TX stream
- Metrics over BLE
- Coredumps over BLE
- App-level retransmission
- Multi-message send window
- Gateway implementation
