# HyperFile Bluetooth transfer protocols

## OBEX Object Push Profile (OPP)

HyperFile implements an initial OBEX OPP receiver over Bluetooth Classic RFCOMM.

- RFCOMM channel: 9
- SDP registration: `sdptool add --channel=9 OPUSH` (requires BlueZ `sdptool`)
- Receive directory: `/userdisk/Favorite/HyperFile`
- Maximum file size: 1 GiB
- Supported operations: OBEX CONNECT, PUT (including multi-packet PUT), DISCONNECT
- Supported headers: Name (Unicode), Length, Body, End-of-Body

The receiver accepts standard OBEX packets and returns Continue while a non-final PUT is in progress, then Success after the final PUT. Received names are treated as basenames; path separators and control characters are rejected. Existing files are not overwritten.

This is an initial receiver implementation, not yet verified against Android, iOS, or a physical dictionary pen. The BlueZ SDP registration command and RFCOMM channel behavior must be checked on the target firmware. OBEX authentication, resumable transfers, and the OBEX client/send direction are not implemented yet.

## HFBT v1 (HyperFile-to-HyperFile)

The existing custom RFCOMM protocol remains available separately on channel 7. Both endpoints must implement HFBT v1; ordinary OBEX-only Bluetooth sharing clients cannot use it.
