# HyperFile Bluetooth transfer protocol (HFBT v1)

HyperFile uses Bluetooth Classic RFCOMM directly; it does not require OBEX.

- RFCOMM channel: 7
- Service role: each endpoint can listen and send
- Link: Bluetooth Classic RFCOMM; pair/trust the peer before connecting
- Maximum accepted file size: 1 GiB
- Receive directory on HyperFile: `/userdisk/Favorite/HyperFile`

## Frame

All integers are unsigned big-endian.

| Field | Size | Value |
|---|---:|---|
| Magic | 4 bytes | ASCII `HFBT` |
| Filename length | 4 bytes | UTF-8 byte count, 1–255 |
| File size | 8 bytes | Exact payload size |
| Filename | N bytes | UTF-8 basename only |
| Payload | File size | Raw file bytes |

The receiver must reject unknown magic, invalid lengths, oversized files, and names containing path separators or control characters. The destination name must be treated as an untrusted basename; never use a received path.

## Compatibility

Both sending and receiving endpoints must implement this exact framing and listen on RFCOMM channel 7. A generic phone Bluetooth file-share UI that only supports OBEX will not interoperate. A companion client/app is required on the other device.

The current HyperFile native module implements this framing for one file per RFCOMM connection. It does not yet provide encryption independent of the Bluetooth link, resume, directory transfer, or a phone-side companion app.
