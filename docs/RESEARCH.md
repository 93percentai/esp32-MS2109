# Prior MS2109 work and implementation choices

Research checked 10 October 2026. Relevant prior work exists for ESP32-P4,
which is useful because it also has USB High-Speed host and a hardware JPEG
decoder. This does not establish that the exact S31/card combination is tested.

| Source | Finding used here |
| --- | --- |
| [netube99/sskvm](https://github.com/netube99/sskvm) (`949ba81b0b0b531386888fb93654cad01d2f0386`) | Receives MS2109 MJPEG on P4 HS USB; uses 16384-byte URBs and PSRAM frame buffers. Its `components/usb_host_uvc/PATCHES.md` identifies continuation headers embedded in JPEG and stop/frame-allocation races. |
| [wxffxx/ExoAnchor](https://github.com/wxffxx/ExoAnchor/blob/ba809edfd86a8b584fe6dcb1bb8e60043ccc0672/device/ESP32P4/README.md) | Documents known-good P4 external-capture UVC implementations and separates verified hardware from compile-only profiles. Its video input uses MJPEG validation and 16 KiB URBs. |
| [Alu's ESP32-S3 KVM attempt](https://alu.dog/posts/failed-project-an-esp32-s3-based-KVM-solution/) | The author reports malformed MS2109 descriptors at Full Speed and failed enumeration. The S3 result is a warning about USB speed, not evidence that the S31 must fail. |
| [Espressif Korvo guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/user_guide.html) | S31 board and LCD-subboard wiring; use its USB Type-A HS port and UART port for programming. |
| [Espressif S31 BSP](https://github.com/espressif/esp-bsp/tree/master/bsp/esp32_s31_korvo_1) | LCD and GT1151 touch via LVGL 9; BSP USB host initialization. Registry version 1.0.1 is pinned. |
| [Espressif UVC host driver](https://github.com/espressif/esp-usb/tree/cfa15aea7266b9b08c9cd5679414416fdd871d3a/host/class/uvc/usb_host_uvc) | S31 target, MJPEG negotiation, frame ownership/return API and mode enumeration. Vendored 2.6.0 plus documented local patches. |

The P4 projects transcode to H.264 or implement KVM input controls. Neither is
needed here: pass the card's JPEGs directly to the browser and decode only for
the LCD. Trim padding after JPEG EOI and publish frames only after successful
decode/dimension validation, so malformed frames are counted and dropped.
Start at VGA to reduce radio load and memory demands. No EEPROM
modification or vendor firmware flashing is needed for this implementation.

The card's VID/PID, descriptors, advertised modes and header behavior vary
between products. The quirk is deliberately limited to known IDs and boundary
signatures. Other IDs use upstream parsing and need device-specific evidence
before enabling a new quirk. There was no verified MS2109 + S31 result found
in this search; actual hardware verification remains outstanding.
