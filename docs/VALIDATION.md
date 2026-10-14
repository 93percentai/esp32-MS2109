# ESP32-S31 Korvo-1 + MS2109 wireless display

ESP-IDF firmware that receives HDMI video from an MS2109 USB capture card,
shows it on the Korvo-1's **800×480 LCD subboard**, and serves a live MJPEG page
over the local Wi-Fi network. The USB card supplies JPEG frames; the S31 JPEG
engine decodes them into RGB565; PPA scales and composes the LCD image with
DMA2D transfers. The image is letterboxed without
changing aspect ratio. HDMI audio is captured as **48 kHz stereo PCM16**,
played through the Korvo ES8389 codec and available in the browser.

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
6. Click **Enable sound** for browser audio. Browser autoplay rules require this
   click. Browser volume and device-speaker volume/mute are separate controls.
   Attach a compatible speaker to the Korvo audio output; the board speaker
   receives a mix of both HDMI channels. Device volume defaults to 35%.
7. Tap the LCD to show/hide image details: dimensions, capture FPS, JPEG size,
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

For an existing checkout, enable `CONFIG_LV_USE_PPA=y`,
`CONFIG_LV_USE_PPA_IMG=y`, `CONFIG_LV_DRAW_BUF_ALIGN=64` and
`CONFIG_LV_PPA_BURST_LENGTH=64` in `firmware/sdkconfig`. Also set
`CONFIG_LWIP_TCP_SND_BUF_DEFAULT=65534` and `CONFIG_LWIP_TCP_WND_DEFAULT=32768`
for the streaming transport. The build checks the required PPA settings, so an
old configuration cannot silently retain CPU rendering.

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
older publications when it falls behind. USB capture publishes after bounded JPEG
header/EOI validation and promptly returns its buffers; it never waits for LCD
decoding, PPA or LVGL. The LCD owns its JPEG copy until DMA finishes. Status and snapshot
requests use a separate HTTP task. USB unplug/replug and a 10-second capture
stall trigger stream cleanup/reopen. The page retries after capture outages.
Frames older than 3 seconds are hidden/refused rather than presented as live.

Each MJPEG frame has a **250 ms send budget**, shared across all partial socket
writes and HTTP chunks. A stalled client is closed and the viewer reconnects;
queued TCP bytes and browser buffering are outside that application budget.
If `stream_aborts` rises on a weak link, use the VGA capture profile or change
`Maximum MJPEG frame send time` in menuconfig. Wi-Fi power saving is disabled.
The TCP send window defaults to about 64 KiB instead of 5.6 KiB.

`/status`, LCD touch details and `/logs` distinguish capture rate from LCD
submission and network-send rates. Timing fields are `jpeg_decode_us`,
`ppa_scale_us`, `present_us` (LVGL submission/lock), `display_age_us` (publication
to LCD submission), and `stream_send_us` (socket enqueue time). They do not
measure HDMI-to-photon or browser rendering latency. Default 720p hardware
scaling is exactly 0.625 on both axes; custom modes use the same quantized
1/16-step PPA factor on both axes and may have slightly larger black borders.

## HDMI sound

The pinned USB Audio Class driver shares the existing USB host with video.
Known MS2109 IDs `345f:2109` and `534d:2109` are supported, using the card's
48 kHz, two-channel, 16-bit PCM audio input. Audio is taken from HDMI;
the board microphone is not captured. Cards without that advertised format
remain video-only and log an audio negotiation error.

USB audio capture, I2S speaker playback and browser delivery use separate
workers. Audio is retained in six 20 ms blocks in PSRAM. Consumers take their
own copies and skip old blocks when more than 60 ms behind. The browser
uses Web Audio with a small playback buffer; delayed/discontinuous audio is
flushed instead of growing a long queue. It works on the device's HTTP LAN
page without HTTPS/AudioWorklet. Network blocks have a 100 ms send deadline.
One audio listener is supported independently of the one video viewer.

`MS2109 wireless display` menuconfig settings allow disabling audio capture or
local speaker playback and changing the initial volume. Existing configurations
pick up these new options automatically. Browser sound starts muted until the
user clicks **Enable sound**. The device speaker plays automatically at its
configured volume, even while setting up Wi-Fi. Speaker settings are runtime
settings and reset to the configured default after reboot.

Audio and MJPEG video have independent buffers; sample-accurate A/V sync is
not guaranteed. Verify lip sync and real latency with a changing HDMI source.
An unavailable speaker codec does not prevent browser audio or video capture.
Use `/audio/status` and `/logs` to diagnose audio connection, dropped blocks
and codec/I2S errors.

## HTTP endpoints

| URL | Response |
| --- | --- |
| `http://DEVICE_IP/` | Browser viewer with reconnect, snapshot and details controls |
| `http://DEVICE_IP/logs` | Auto-refreshing device logs with download button |
| `http://DEVICE_IP/logs.txt` | Plain-text recent application logs |
| `http://DEVICE_IP/status` | JSON capture and Wi-Fi metrics |
| `http://DEVICE_IP/snapshot.jpg` | Latest fresh JPEG; 503 when unavailable |
| `http://DEVICE_IP/audio/status` | Audio connection, format, speaker settings and counters |
| `http://DEVICE_IP/audio/control` | POST form `volume=0..100&mute=0/1` for the device speaker |
| `http://DEVICE_IP:82/audio.pcm` | Framed PCM stream for one browser audio listener |
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
including rejecting stale frames after capture disconnects. Audio tests check
PCM consumer ownership, backlog limits, reconnect generations, mono mixing and
control validation. With Node.js installed they also check C-to-JavaScript packet
framing, signed stereo playback, user-gesture start/stop and jitter scheduling. Log-ring tests
check wrapping, oversized writes, overwrite counts and bounded snapshots.
See [THIRD_PARTY.md](THIRD_PARTY.md) for reused code and licenses.

## HDMI audio update

The supplied UART descriptor log identifies a separate AudioStreaming interface
3, endpoint 0x82 IN isochronous, 192-byte maximum packets, High-Speed interval
4 (1 ms). Its format bytes describe two PCM16 channels at 48000 Hz. Managed
`usb_host_uac` 1.5.0 supports ESP32-S31 and shares the existing USB host with
UVC. Capture negotiates that exact audio format for known MS2109 VID/PIDs.
The application does not capture the board microphone.

USB callback events only set flags/queue connection and removal handles. One
application task owns audio open/read/close, including deferred deletion after
the driver's disconnect callback. Partial reads accumulate into 20 ms blocks.
Playback and HTTP consumers copy from a bounded six-block PSRAM ring, each with
its own cursor; a slow consumer skips to current audio. Stream generations
reset on disconnect. Consumers reject data older than 100 ms. Local playback
mixes stereo into dual mono, uses the Korvo BSP ES8389 codec and I2S DMA, and
feeds silence during missing data. Codec errors do not stop video/browser audio.

The browser fetches framed PCM on independent port 82 after a user gesture.
Its decoder handles arbitrary HTTP fragmentation/coalescing with fixed-size
buffers, validates the PCM1 header, and schedules signed stereo samples using
Web Audio. Continuous packets share a sample timeline; discontinuities and
excess playback backlog flush queued nodes. Disabled sound, page exit and idle
network timeouts abort fetch/playback. Device-speaker and browser gain controls
are separate; device settings reset after reboot. Audio and MJPEG buffers are
independent and do not guarantee precise A/V synchronization.

Host sanitizer tests cover PCM-ring bounds, independent consumer ownership,
slow-consumer skips, sequence wrap, disconnect generations, wire encoding,
mono-mix extrema and malformed volume/mute forms. Node tests consume a fixture
from the actual C wire encoder and check chunk fragmentation, coalescing,
malformed/truncated packets, signed stereo samples, no autoplay, jitter-resistant
continuous scheduling and disabling/flushing playback.

Physical acceptance: provide HDMI with left-only/right-only tones, then speech
and a click/flash test. Verify both channels in the browser and a mix on the board
speaker. Test local mute/volume separately from browser sound. Open video and
audio together, monitor capture/PPA/audio counters, and check for CPU/heap loss,
crackling, drift or increased video latency. Repeatedly unplug/replug USB and
stop/restart HDMI; audio must recover without retaining old samples or handles.
Throttle browser audio: it must reconnect/drop delayed blocks while video and
local playback continue. Confirm sound during Wi-Fi AP setup and after reconnect.
No board, speaker or MS2109 is attached to the workspace; audible playback,
USB isochronous timing and end-to-end A/V latency remain hardware checks.

Audio-update checks passed with ESP-IDF v6.1 for ESP32-S31: application image
1,662,728 bytes in the 6 MiB partition and static DIRAM 169,152 bytes. All
ASan/UBSan host suites and both browser PCM/framing/playback checks passed.
The managed dependency lock pins USB UAC 1.5.0 and the existing USB host 1.6.0.
