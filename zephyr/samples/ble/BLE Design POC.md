# BLE Design/POC

# GATT Service Design

Base UUID: `2653xxxx-81E5-4861-82AE-2C92E6887922`

Service UUID: `26530001-81E5-4861-82AE-2C92E6887922`

## GATT Characteristics

| **Name** | UUID fragment | **Properties** | **Description** |
| --- | --- | --- | --- |
| Capabilities | `0002`  | READ | Protocol verson 2 bytes 0x01 for the initial version |
| Device Id | `0003`  | READ | Device Id to use to connect to Spotflow. |
| Session Metadata | `0004`  | READ | Used by the gateway each time it (re)connects via MQTT. CBOR bytes of the message directly relayed by the gateway into the `ingest-cbor`  MQTT topic. |
| TX Stream | `0005` | NOTIFY | Device will stream all the telemetry through this characteristic. Messages are framed due to MTU constraints. |
| RX Stream | `0006` | WRITE without response | Commands sent from the gateway to control the peripheral. E.g. ACK/NACK/C2D messages.  Messages are framed due to MTU constraints. |

#### To be considered in future

| **Name** | **Properties** | **Description** |
| --- | --- | --- |
| Authentication | READ | When ingest key is provided by Gateway |

## Stream Characteristics

### Framing

Both TX and RX streams are using a framing format that wraps the actual data. Some message types may be fragmented into multiple frames. This allows sending messages longer than the negotiated MTU size (which might be as low as 23 bytes).

### Message Types

Every message has assigned a specific message type. The message type is embedded into the first byte of each frame. Thanks to that, the receiver can always parse the first byte and then decide how to parse the rest.

| Message Type | Byte Identificator | Is Fragmented | Allowed in |
| --- | --- | --- | --- |
| `ACK` | `0x00` | NO | TX/RX |
| `NACK`  | `0x01` | NO | TX/RX |
| `TELEMETRY`  | `0x02` | YES | TX |
| `REPORTED_CONFIGURATION`  | `0x03` | YES | TX |
| `DESIRED_CONFIGURATION` | `0x04` | YES | RX |

### Multiplexing

Since we use a single characteristic in each direction, any kind of multiplexing must be implemented in the clients, if needed. E.g. the device is able to send OTA status updates while waiting for acknowledgement from the gateway processing a coredump chunk.

## Message Format

The general framing format is as follows:

```jsx
Byte   0:   Message Type
Byte  1+:   Data
```

## Fragmented Frames

Currently used for `TELEMETRY` , `REPORTED_CONFIGURATION` , and `DESIRED_CONFIGURATION` .

The pair (message type, sequence number) identifies a single message in a rolling window of 256 messages of that type. This allows multiplexing at the level of message types, e.g. when waiting for ACKs from one message type stream, we don’t need to block other types.

```jsx
First fragment:
	Byte  0:   Message Type
  Byte  1:   Flags
  Bytes 2:   Message Sequence Number (uint8_le, rolling)
  Bytes 3-4: Total Message Byte Length (uint16_le)
  Bytes 5+:  Fragment Data

Continuation fragment:
  Byte   0:    Message Type
  Byte   1:    Flags
  Byte   2:    Sequence Number
  Bytes 3+:    Fragment Data
```

Flags:

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | `IS_FIRST` | First (or only) fragment of a new message |
| 1 | `IS_LAST` | Last (or only) fragment of a message |
| 2 | `NEEDS_ACK` | Receiver must send `ACK` before sender sends the next message of the specific type. All frames of the message must have this flag set to the same value.  |
| 3-7 | reservered | set to 0 (should not be checked by the receiver) |

### ACK/NACK messages

These messages are always sent in a single frame.

```jsx
ACK:  0x00 || uint8 message_type || uint8 seq
NACK: 0x01 || uint8 message_type || uint8 seq
```

### Quality of Service

The messages are sent via `NOTIFY` and `WRITE` properties, which results at a best effort delivery at the transport level. This allows higher throughput for the majority of messages.

#### App-level at-least-once delivery (Not to be implemented for the POC)

For messages that require higher guarantees, we use app-level acknowledgements. Each message, that needs at-least-once delivery, must contain the `NEEDS_ACK` flag in every frame.

The receiver must:

- When last fragment with `NEEDS_ACK` is sucessfully received, the receiver must send back the `ACK` message.
- When last fragment with `NEEDS_ACK` is received, but the lengths do not match, the receiver must send back the `NACK`  for that message.
- When a continuation fragment with `NEEDS_ACK` is received on its own, the receiver must send `NACK`  for that message.
- When a message with the same message type and sequence number is received within the same BLE session with `NEEDS_ACK`, the receiver must send `ACK` again for that message.

The sender must:

- Wait for the `ACK` message before sending a next message of the same type.
- Define a timeout after which it retransmits the not acked message.
- Retransmit a message for which a `NACK` message was received.

## Gateway Flow

### Connection Establishment

1. Scan for and pair a device with Spotflow GATT service advertised
2. Read Capabilities Characteristic → check version compatibility
3. Initial Information Read
    1. Read `Device Id`
    2. Read `Session Metadata`
4. Establish a new MQTT Connection with the obtained device id and gateway-provided ingest-key
    1. Connect to `ingest-cbor` , `config-cbor-d2c` for writes and `config-cbor-c2d` for reads
    2. Publish session metadata into `ingest-cbor`
5. CCCD Write: Enable notify on `TX stream`

### Configuration Handling

1. On `REPORTED_CONFIGURATION`  message from the `TX Stream` BLE characteristic, redirect the message to the `config-cbor-d2c` topic
    1. If ACK required
        1. If success, send ACK to the device via `RX Stream`
        2. If failure (e.g. received final frame, but length does not match), send NACK
2. On message from the MQTT `config-cbor-c2d` , redirect the message via write to the `RX Stream` characteristic using the `DESIRED_CONFIGURATION` message

### Telemetry Streaming

1. On `TELEMETRY` message from the BLE `TX Stream` characteristic, redirect the message to the `ingest-cbor`topic
    1. If ACK required
        1. If success, send ACK to the device via `RX Stream`
        2. If failure (e.g. received final frame, but length does not match), send NACK

### MQTT Reconnection Handling

1. MQTT Connection lost
2. CCCD Write: Disable notify on `TX stream`
3. MQTT Reconnects
4. Read `Session Metadata` characteristic
5. Publish `Session Metadata` into `ingest-cbor`
6. CCCD Write: Enable notify on `TX stream`
