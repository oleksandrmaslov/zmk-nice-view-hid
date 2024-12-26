# Nice View with HID support

This [ZMK module](https://zmk.dev/docs/features/modules) adds custom nice!view widget with [Raw HID](https://github.com/zzeneg/zmk-raw-hid) support.

<img src="https://github.com/user-attachments/assets/b8a22942-2aa9-4bb6-b073-99535272cd56"  width="400">

## Features

This widget is based on default nice!view widget and adds additional information about current time, layout and volume level. This information is received from host computer using [companion app](https://github.com/zzeneg/qmk-hid-host) over HID interface.

Differences with default nice!view widget:

- WPM graph is removed
- active profile is displayed as number instead of five circles

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
| `CONFIG_NICE_VIEW_HID_TWO_PROFILES` | Show only two connected profiles as circles | n       |
| `CONFIG_NICE_VIEW_HID_SHOW_LAYOUT`  | Show current layout                         | y       |
| `CONFIG_NICE_VIEW_HID_LAYOUTS`      | Comma-separated list of layouts             | EN      |
| `CONFIG_NICE_VIEW_HID_INVERTED`     | Invert widget colors                        | n       |
