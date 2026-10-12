# ESP32-S31 Korvo-1 + MS2109 wireless display

ESP-IDF firmware that receives HDMI video from an MS2109 USB capture card,
shows it on the Korvo-1's **800×480 LCD subboard**, and serves a live MJPEG page
over the local Wi-Fi network. The USB card supplies JPEG frames; the S31 JPEG
engine decodes them into RGB565 for the LCD. The image is letterboxed without
changing aspect ratio. Audio capture is not implemented.

Implemented and cross-compiled for **ESP32-S31**, not ESP32-S3. Cloud checks
cannot establish USB-card, LCD, touch or radio operation on physical hardware.
See [hardware validation](docs/VALIDATION.md) for the remaining checks.

## Connect and use

1. Attach the compatible LCD/touch subboard (ESP32-S3-LCD-EV-Board-SUB3).
2. Power the Korvo board and connect the MS2109 to its onboard **USB 2.0 HS
   Type-A host port**. Connect the HDMI source to the capture card.
3. On first boot, join **ESP_MS2109_SETUP**, then open **http://192.168.4.1**
   if the captive portal does not open automatically.
4. Choose a scanned Wi-Fi network (or type a hidden SSID), enter its password,
   and press **Test & Save**. Open networks accept a blank password. A failed
   test leaves setup available. Credentials are saved only after DHCP succeeds.
5. Rejoin your usual Wi-Fi network and open the address shown on the LCD or
   serial log, **http://DEVICE_IP/**. The page loads video from port **81**.
6. Tap the LCD to show/hide image details: dimensions, capture FPS, JPEG size,
   dropped/error frames, decode/scale time and free PSRAM. Hold for **2 seconds**
   to reopen Wi-Fi setup. If saved Wi-Fi cannot connect for 30 seconds, setup
   reopens automatically. Local capture continues while setting up Wi-Fi.

The Wi-Fi setup is adapted from your
[Ethernet-via-Wi-Fi project](https://github.com/93percentai/esp32-ethernetviawifi).
This device uses a separate setup SSID and 192.168.4.1 instead of 192.168.1.1.
Use a phone/PC on the same LAN; client isolation can prevent access. The setup
AP is open, as in the original project, and stops after credentials are saved.

## Build and flash

Use [ESP-IDF **v6.1**](https://github.com/espressif/esp-idf/releases/tag/v6.1)
with S31 preview support. Tested SDK commit:
`fff9895c82d744c7237be8847347bdd1b07c6643`. Older S3 SDKs are insufficient.
Install the S31 tools using ESP-IDF's installer, then export its environment:

```sh
cd firmware
idf.py --preview set-target esp32s31
idf.py --preview build
idf.py --preview -p /dev/ttyUSB0 flash monitor
```

Use the **UART USB-C** port to flash/debug, and select the actual serial port.
Hold BOOT and press/release RESET if download mode is needed. The USB-A host
port is reserved for the capture card. Default configuration selects the
board's 16 MB flash and 250 MHz PSRAM configuration from the factory example.
The application partition is 6 MB; no OTA updater is included.

For an existing checkout, set `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=4096`
in `firmware/sdkconfig` (or use `menuconfig` → USB-OTG → largest default-endpoint
transfer size). Defaults only apply when creating a configuration. The build
rejects the old 256-byte setting: MS2109 configuration descriptors can exceed
1 KiB and otherwise fail enumeration before the UVC driver sees the card.

In the prepared cloud workspace, first source
`/workspace/setup/activate-idf61.sh`. It selects the tested SDK and compiler.

## Capture settings and limitations

The default capture profile is **1280×720 MJPEG at 30 fps** for widescreen HDMI.
It is advertised by the user's `345f:2109` card. The browser receives the full
720p JPEG; the LCD shows an **800×450** image with 15-pixel top/bottom bars.
The LCD decoder outputs native little-endian RGB565 for LVGL.

Use `idf.py --preview menuconfig` → `MS2109 wireless display` → `Capture profile`
to choose **640×480 at 30 fps** for a 4:3 source, or a custom advertised mode
(up to 1280×720). The new profile defaults to 720p even in existing configurations;
old custom width/height/FPS fields apply only when selecting the custom profile.
The driver logs the advertised and negotiated modes. A camera that rejects the
requested dimensions or rate produces an error and retry, instead of silently
streaming another resolution. There is no YUY2 fallback or H.264 transcoding.

Compressed frame buffers default to **512 KiB** each. If the card exceeds that,
raise `Maximum compressed frame size` (up to 2 MiB) and check free PSRAM.
USB frames, JPEG DMA buffers and LCD buffers live in PSRAM. USB transfer buffers
use the host driver's required memory capabilities.

USB matching defaults to any UVC device to accommodate different card VID/PID
values. Set VID/PID in menuconfig to restrict it. The continuation-header quirk
is enabled only for `534d:2109` and `345f:2109`, MJPEG and 16 KiB bulk URBs.
See the [research](docs/RESEARCH.md) and
[driver patch notes](firmware/components/usb_host_uvc/PATCHES.md).

One browser can use the continuous stream at a time. Slow clients skip frames;
capture and the LCD do not wait for network transmission. The LCD worker skips
older queued frames when it falls behind and returns those USB buffers promptly. Status and snapshot
requests use a separate HTTP task. USB unplug/replug and a 10-second capture
stall trigger stream cleanup/reopen. The page retries after capture outages.
Frames older than 3 seconds are hidden/refused rather than presented as live.

## HTTP endpoints

| URL | Response |
| --- | --- |
| `http://DEVICE_IP/` | Browser viewer with reconnect, snapshot and details controls |
| `http://DEVICE_IP/logs` | Auto-refreshing device logs with download button |
| `http://DEVICE_IP/logs.txt` | Plain-text recent application logs |
| `http://DEVICE_IP/status` | JSON capture and Wi-Fi metrics |
| `http://DEVICE_IP/snapshot.jpg` | Latest fresh JPEG; 503 when unavailable |
| `http://DEVICE_IP:81/stream` | Multipart MJPEG stream |

The logs also work during Wi-Fi setup at **http://192.168.4.1/logs**. They
retain the most recent 16 KiB of ESP application logs in RAM (configurable),
including enumeration errors, USB identity/speed, advertised modes, stream
start and JPEG failures. Download the logs after connecting/reconnecting the
card if capture still fails. The buffer resets on reboot and reports overwritten
or truncated bytes; ROM/bootloader and raw `printf` descriptor dumps remain
serial-only. Serial logging continues alongside browser logging.

These are local HTTP endpoints without authentication. Keep them on your
trusted LAN. They do not require an internet service or a browser camera plugin.

## Checks

```sh
sh tests/run.sh
cd firmware
idf.py --preview build size
```

Host tests exercise malformed/overlong credentials, SSID escaping, MS2109
payload-header boundaries and aspect-preserving RGB565 scaling with padded
source strides, including 720p colour bars and a scaler reference comparison.
The actual UVC bulk callback is tested with ZLP delimiters, continuation/final
headers, frame-ID changes, error flags and standard-camera packets.
They use AddressSanitizer and UndefinedBehaviorSanitizer.
They also check captive DNS packet bounds and concurrent snapshot ownership,
including rejecting stale frames after capture disconnects. Log-ring tests
check wrapping, oversized writes, overwrite counts and bounded snapshots.
See [THIRD_PARTY.md](THIRD_PARTY.md) for reused code and licenses.
