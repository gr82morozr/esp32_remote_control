# Schema Metadata Format

This document describes the schema metadata sent by ESP32 Remote Control
devices so external apps can parse telemetry without hardcoded field names.

## Transport

Schema metadata is sent as normal `RCMessage_t` frames with:

```cpp
msg.type = RCMSG_TYPE_SCHEMA;  // value 6
```

The 25-byte `RCMessage_t::payload` contains one packed `RCSchemaChunk_t`:

```cpp
struct RCSchemaChunk_t {
  uint8_t schema_id;     // Schema stream identifier
  uint8_t chunk_index;   // Zero-based chunk number
  uint8_t chunk_count;   // Total chunks in this schema message
  uint8_t text_len;      // Number of valid bytes in text
  char text[21];         // ASCII schema text bytes, not necessarily null-terminated
};
```

All multi-byte fields in data payloads are little-endian. Schema chunk fields
are single-byte except `text`.

## Reassembly Rules

1. Start a new schema when `chunk_index == 0`.
2. Keep only chunks with the same `schema_id` and `chunk_count`.
3. Append exactly `text_len` bytes from `text`.
4. Chunks must arrive in order: `0, 1, 2, ... chunk_count - 1`.
5. Reject a schema if `chunk_count == 0`, `chunk_index >= chunk_count`, or
   `text_len > 21`.
6. When `chunk_index == chunk_count - 1`, the concatenated ASCII text is the
   complete schema string.

The serial bridge forwards a completed text schema as:

```text
RC_SCHEMA:<schema text>
```

Binary bridge output suppresses the `RC_SCHEMA:` text line, but raw
`RCMSG_TYPE_SCHEMA` frames still use the chunk format above.

## Resend Interval

The included dummy sensors send schema metadata after the link is connected and
then every `RC_SCHEMA_INTERVAL_MS`. The default is:

```cpp
#define RC_SCHEMA_INTERVAL_MS 10000
```

So the default resend rate is every 10 seconds.

## Schema Text Grammar

Schema text is ASCII and currently uses this compact grammar:

```text
n=<schema_name>;f=<field>,<field>,...
```

Each field uses colon-separated tokens:

```text
<wire_name>:<type>:<scale_or_unit>[:<label>]
```

Token meanings:

| Token | Meaning |
|-------|---------|
| `schema_name` | Short name for the payload layout. |
| `wire_name` | Compact field name used by telemetry output. |
| `type` | Storage type: `u8`, `u16`, `u32`, or `i16`. |
| `scale_or_unit` | Numeric scale such as `.01`, raw scale `1`, or unit token such as `us` or `ms`. |
| `label` | Optional semantic/display label. |

If `scale_or_unit` is numeric, convert a raw integer value with:

```text
decoded_value = raw_value * scale
```

If `scale_or_unit` is a unit token, keep the raw value and apply that unit.

## Included Dummy Sensor Schema

The current dummy sensor schema is:

```text
n=i16x8t;f=seq:u16:1,s_us:u32:us,v0:i16:.01:temp,v1:i16:.001:volt,v2:i16:.01:cmd1,v3:i16:.01:cmd2,v4:i16:1:id1,v5:i16:1:id2,v6:i16:1:cflg,v7:i16:ms:dt,fl:u8:seen,r1:u8:sid,r2:u8:sver
```

It describes this packed 25-byte telemetry payload:

```cpp
struct RCPayload_I16x8_Time_t {
  uint16_t seq;        // bytes 0-1
  uint32_t sample_us;  // bytes 2-5
  int16_t value[8];    // bytes 6-21
  uint8_t flags;       // byte 22
  uint8_t reserved1;   // byte 23, schema ID
  uint8_t reserved2;   // byte 24, schema version
};
```

Little-endian binary format for PC parsing:

```text
<HI8hBBB
```

Field mapping:

| Wire name | Payload field | Type | Scale/unit | Label |
|-----------|---------------|------|------------|-------|
| `seq` | `seq` | `u16` | `1` | sequence counter |
| `s_us` | `sample_us` | `u32` | `us` | sample timestamp |
| `v0` | `value[0]` | `i16` | `.01` | `temp` |
| `v1` | `value[1]` | `i16` | `.001` | `volt` |
| `v2` | `value[2]` | `i16` | `.01` | `cmd1` |
| `v3` | `value[3]` | `i16` | `.01` | `cmd2` |
| `v4` | `value[4]` | `i16` | `1` | `id1` |
| `v5` | `value[5]` | `i16` | `1` | `id2` |
| `v6` | `value[6]` | `i16` | `1` | `cflg` |
| `v7` | `value[7]` | `i16` | `ms` | `dt` |
| `fl` | `flags` | `u8` | `1` | `seen` |
| `r1` | `reserved1` | `u8` | `1` | `sid` |
| `r2` | `reserved2` | `u8` | `1` | `sver` |

For the included dummy sensor, `reserved1` carries the schema ID and
`reserved2` carries the schema version.

