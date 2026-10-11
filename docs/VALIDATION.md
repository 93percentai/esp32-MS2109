# Validation

## Automated checks

The implementation was cross-compiled with ESP-IDF v6.1 for `esp32s31`, including
the official Korvo BSP, RGB LCD/GT1151 touch, hardware JPEG and USB host driver.
The dependency lock records the resolved managed components. `idf.py size`
checks the configured image/partition memory budget.
The resulting image is approximately 1.53 MB in the 6 MB application partition;
static DIRAM use is approximately 149 KB. Runtime PSRAM allocations and real
radio/USB heap use must still be checked on the board.

`sh tests/run.sh` checks form decoding/escaping, malformed and overlong values,
32-byte SSIDs, boundary-safe MS2109 header recognition, RGB565 letterboxing,
source-stride padding and buffer bounds under ASan/UBSan. It also checks bounded
captive DNS replies, stale/disconnected snapshot rejection, retained consumer
copies, and concurrent frame publication/copy using host mutexes. These tests
passed; host mutexes do not emulate the entire FreeRTOS/USB stack.

No serial board or USB capture card is attached to the cloud environment.
Cross-compilation does not verify the video path, power, actual card descriptors,
Wi-Fi radio behavior, color ordering, touch events or end-to-end frame rates.

## Physical acceptance checks

1. Flash via UART USB-C. Confirm `esp32s31`, PSRAM detection and RGB LCD init in
   the boot log. Verify the MS2109 enumerates at High Speed and advertises VGA
   MJPEG. Save the full descriptor/mode log if enumeration or streaming fails.
2. With no saved Wi-Fi, join `ESP_MS2109_SETUP` and open `192.168.4.1`. Test an
   incorrect password: setup must remain open and no new profile should persist.
   Test a correct password: wait for DHCP, save, and confirm the AP disappears.
3. Open the LCD's LAN address. Confirm video in the browser and on the LCD at
   once. Test color bars, a timestamp and a changing scene; check RGB ordering,
   aspect ratio, USB header corruption and freshness.
4. Tap once: details show. Tap again: details hide. Hold 2 seconds: Wi-Fi setup
   reopens and local capture continues. Short taps must not trigger setup.
5. Reboot: saved Wi-Fi reconnects. Turn the router off for 30 seconds: setup
   returns. Confirm setup can replace a saved profile without reflashing.
6. Unplug/replug the capture card repeatedly. The LCD must hide stale video,
   `/snapshot.jpg` must return 503, and video must recover after reconnection.
   Disconnect HDMI separately; card-generated no-signal frames may continue.
7. Slow/throttle a browser connection, then disconnect it. USB reception and
   LCD updates must continue, `/status` must respond, and another viewer must
   work after the first closes. Leave capture active for an hour and watch for
   resets, memory loss, accumulating errors and stalled streams.

For mismatched modes, configure a mode from the printed card list. For frame
overflow, increase the compressed buffer cap. For corrupt JPEGs, record VID/PID
and payload-boundary evidence before changing the driver quirk. High capture
FPS is not a promise of equivalent Wi-Fi or LCD FPS.

## Detection and browser logs update

Raised the USB enumeration control-transfer cap from 256 to 4096 bytes and
added a compile-time guard for existing configurations. Added `/logs` and
`/logs.txt` on both LAN and setup AP, retaining 16 KiB of application logs
while preserving serial output. Host sanitizer tests cover ring wrap, oversized
writes, overwrite accounting and snapshot bounds against a reference history.

Verification passed: ESP-IDF v6.1 `esp32s31` preview build and size check
with 4096-byte control transfers and the 16 KiB log buffer; all ASan/UBSan
host tests; JavaScript syntax and simulated log-page refresh/download/error
handling. Application image: 1,526,872 bytes. No physical board was attached.

Hardware follow-up: open the logs page before reconnecting the card; confirm
VID/PID, High Speed, descriptor length, modes, stream start and first decoded
frame. Download logs if any stage fails. Confirm logs refresh during streaming
and setup, downloads preserve text, and old logs expire with an overwrite count.
