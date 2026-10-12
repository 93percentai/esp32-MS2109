# Vendored driver and local changes

Espressif `usb_host_uvc` 2.6.0 from `espressif/esp-usb` commit
`cfa15aea7266b9b08c9cd5679414416fdd871d3a`, Apache-2.0 (see LICENSE).
Tests and examples are omitted. The manifest uses the registry USB host component
instead of the upstream monorepo relative override.

Changes in `uvc_bulk.c`, `uvc_host.c`, `uvc_control.c`, and `private_include/`:

- Strip 12-byte PTS/SCR/EOH headers at bulk URB boundaries for MJPEG from
  `534d:2109` / `345f:2109` with 16384-byte URBs. Handle same-FID continuation
  and final payloads, preserve final JPEG bytes, complete on EOF, propagate ERR,
  and recover frame-ID changes without joining two JPEGs. Enabled by
  `CONFIG_CAPTURE_MS2109_HEADERS`; do not scan arbitrary JPEG bytes.
- Treat zero-length bulk packets as delimiters without marking a frame invalid,
  including while waiting for SOF/EOF. This also applies to standard devices.
- Propagate failed probe SETs and reject returned dimensions/rate/format that
  differ from an explicit request; refuse mismatched modes at commit. Log the
  negotiated resolution and frame rate.
- Recheck streaming under the driver lock after acquiring a free frame. Return
  that frame if pause won the race; guard NULL before resetting an existing frame.
- Log enumerated device VID/PID, bus speed and configuration length through
  ESP logging so browser diagnostics include them.
- Bound diagnostic byte reads and MJPEG SOI reads for short transfers.

The header-boundary quirk and pause race were identified by
[netube99/sskvm](https://github.com/netube99/sskvm/blob/949ba81b0b0b531386888fb93654cad01d2f0386/components/usb_host_uvc/PATCHES.md).
This is a scoped adaptation against upstream 2.6.0, not the P4 project's whole
modified driver. It still needs verification with the user's actual card.
Preserve these changes when upgrading upstream. Host sanitizer tests exercise the actual bulk callback with simulated transfers,
as well as bounded header recognition.
