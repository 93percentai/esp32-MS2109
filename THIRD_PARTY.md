# Attribution

`firmware/main/config_store.*`, `provisioning.*`, `wifi_mgr.*` and
`firmware/components/dns_server/` originate from
[93percentai/esp32-ethernetviawifi](https://github.com/93percentai/esp32-ethernetviawifi)
commit `cdb7d4fec02a47fa65abc625b5c562e80b286d1d`, MIT license (repository LICENSE).
The DNS component's original Espressif notices specify Unlicense OR CC0-1.0;
these are retained in its source and header.
The AP flow remains scan → select/custom SSID → test → save → stop AP.
This adaptation uses a STA DHCP netif, waits for an IP address, validates full
POST bodies and form escapes, and changes the setup name/address for this device.
The captive DNS parser is bounded to a single standard wildcard question and
its task closes the socket before stop returns, allowing repeated setup sessions.

`firmware/components/usb_host_uvc/` is Espressif's Apache-2.0 driver.
Its LICENSE, source copyright notices and PATCHES.md are retained.
The MS2109 quirk/race findings are credited to netube99/sskvm in PATCHES.md.

The BSP, LVGL, touch, USB and other managed dependencies retain their own
licenses in the downloaded components. `firmware/dependencies.lock` pins them.
