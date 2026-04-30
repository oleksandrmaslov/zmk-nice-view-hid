# Nice View with HID support

This [ZMK module](https://zmk.dev/docs/features/modules) adds custom nice!view widget with [Raw HID](https://github.com/zzeneg/zmk-raw-hid) support.

<img src="https://github.com/user-attachments/assets/b8a22942-2aa9-4bb6-b073-99535272cd56"  width="400">

## Features

This widget is based on default nice!view widget and adds additional information about current time, layout and volume level. This information is received from host computer using [companion app](https://github.com/zzeneg/qmk-hid-host) over HID interface.

Differences with default nice!view widget:

- WPM graph is removed
- active profile is displayed as one of five profile icons
- RAW HID not-ready state shows a `Connect RAW HID` fallback
- media title and artist buffers support longer text

## Installation

To use, first install and configure [companion app](https://github.com/zzeneg/qmk-hid-host) (default product id for ZMK is `0x615E`).

Then add this and [Raw HID](https://github.com/zzeneg/zmk-raw-hid) modules to your `config/west.yml` by adding a new entries to `remotes` and `projects`:

```yaml west.yml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: zzeneg # <-- new entry
      url-base: https://github.com/zzeneg
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-nice-view-hid # <-- new entry
      remote: zzeneg
      revision: main
    - name: zmk-raw-hid # <-- new entry
      remote: zzeneg
      revision: main
  self:
    path: config
```

For more information, including instructions for building locally, check out the ZMK docs on [building with modules](https://zmk.dev/docs/features/modules#building-with-modules).

Replace `nice_view` with `nice_view_hid_adapter` and add the `raw_hid_adapter` as an additional shield to your build, e.g. in `build.yaml`:

```yaml build.yaml
---
include:
  - board: nice_nano_v2
    shield: corne_left nice_view_adapter nice_view_hid_adapter raw_hid_adapter
```

## Configuration

| Name                                | Description                                 | Default |
| ----------------------------------- | ------------------------------------------- | ------- |
| `CONFIG_NICE_VIEW_HID`              | Enable Nice!View HID widget                 | n       |
| `CONFIG_NICE_VIEW_HID_TWO_PROFILES` | Deprecated compatibility option             | n       |
| `CONFIG_NICE_VIEW_HID_SHOW_LAYOUT`  | Show current layout                         | y       |
| `CONFIG_NICE_VIEW_HID_LAYOUTS`      | Comma-separated list of layouts             | EN      |
| `CONFIG_NICE_VIEW_HID_MEDIA_SCROLL` | Reserved compatibility option               | y       |
| `CONFIG_NICE_VIEW_HID_INVERTED`     | Invert widget colors                        | n       |

## UI references

The nice!view UI is recreated for LVGL from the 68x160 SVG references in the keyboard
config repository:

- `references/Connected.svg`
- `references/No RAWHID.svg`
- `references/Peripheral.svg`

The code uses the SVG groups and IDs as the layout source of truth:

- `Battery` for the battery outline and fill area
- `Bluetooth` for the BLE status icon area
- `Language` / `material-symbols:language` for the layout indicator
- `volume` for the volume indicator
- `Profile`, `Connected profile`, `Bounded profile`, and `Free profile` for the five
  profile slots
- `Connect` and `RAW HID` for the RAW HID fallback state
- `mdi:play`, `Playing`, title, and artist text groups for the peripheral media state

No generated C image assets are currently used. The simple monochrome SVG shapes and text are
recreated on an off-screen 68x160 LVGL canvas, then the canvas buffer is rotated into the
visible 160x68 nice!view framebuffer. To regenerate assets in a future image-based version,
convert the SVG groups above to LVGL 9 image descriptors and replace the matching drawing
helpers in `src/widgets/util.c`.

## Media text limits

The firmware-side media title and artist buffers hold 96 bytes plus a null terminator each.
Incoming media packets are copied with bounds checks and are always null-terminated before
raising display events.

The existing RAW HID sender protocol is unchanged: media packets are still
`type, length, UTF-8 bytes`. With the default `CONFIG_RAW_HID_REPORT_SIZE=32`, one USB RAW HID
report can carry up to 30 text bytes after the type and length bytes. Larger text requires a
larger compatible report size and sender support; split peripheral forwarding can also be
limited by the BLE ATT-safe payload size in the raw HID relay. The display side can safely
store up to 96 bytes when those transports provide it.

## Attribution

Battery and output transport state handling follows the approach from
[`kevinpastor/nice-view-elemental`](https://github.com/kevinpastor/nice-view-elemental):
using ZMK battery state, USB power state, selected endpoint transport, BLE active profile,
BLE active profile connection state, and BLE profile bonding/open state. That project is MIT
licensed. No nice-view-elemental image assets are copied into this module.
