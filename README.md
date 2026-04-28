# Nice View with HID support

A [ZMK module](https://zmk.dev/docs/features/modules) that turns the
nice!view side display into a status panel driven by Raw HID. Companion app
support comes from [zmk-raw-hid](https://github.com/zzeneg/zmk-raw-hid) /
[qmk-hid-host](https://github.com/zzeneg/qmk-hid-host).

## What's on screen

The layout is taken from the SVG mockups in [`references/`](../references/)
and rendered directly on the 160×68 nice!view canvas (no portrait/rotate
pipeline — the panel handles rotation itself). All glyphs are 1-bit pixel
art, drawn either with LVGL primitives (battery, profile dots) or as
embedded `LV_COLOR_FORMAT_I1` bitmaps (Bluetooth, USB, language, volume,
profile dots).

| Region | Source SVG group | Notes |
|--------|------------------|-------|
| Battery | `Battery` (rounded 24×12 rectangle + terminal) | Charging bolt drawn over the fill |
| Bluetooth / USB / searching | `Bluetooth` group, plus `mdi:play` for searching | Adapted from [`kevinpastor/nice-view-elemental`](https://github.com/kevinpastor/nice-view-elemental) |
| Time `09:41` | `09:41` text group | Montserrat 18 |
| Language | `Language` group (10×10 globe + EN text) | Icon swappable via `CONFIG_NICE_VIEW_HID_LAYOUTS` |
| Volume | `volume` group (speaker + waves + percentage) | Icon scales with level (mute / mid / loud) |
| 5 profile slots | `Connected profile`, `Bounded profile`, `Free profile` (and the "selected free" hybrid) | See state mapping below |
| Layer name | `Layer` + `Base` text groups | Falls back to `Layer: N` when the layer has no label |
| RAW HID fallback | `No RAWHID.svg` | Two-line `Connect` / `RAW HID` prompt when the host is not talking |
| Peripheral media | `Peripheral.svg` | Marquee-scrolls long titles, see "Media text" |

## Profile state mapping

There are always 5 profile dots (one per BLE slot), each in one of 3 visual
states sourced from the SVG groups:

| Visual | SVG group | Drawn when |
|--------|-----------|------------|
| Filled square | `Connected profile` (`selected_profile.svg`) | slot is the active profile **and** is bonded |
| Outlined square | `Bounded profile` (`Bounded_profile.svg`) | slot is bonded but not active |
| Dotted square | `Free profile` (`Free_profile.svg`) | slot has no bonded peer |
| Dotted-with-fill | `selected_free_profile.svg` | slot is active but the active profile slot is unbonded (pairing mode) |

Mapping in code, per slot `i`:

```
selected = (i == zmk_ble_active_profile_index())
bonded   = !zmk_ble_profile_is_open(i)
```

The dot count is fixed at 5 — `CONFIG_NICE_VIEW_HID_TWO_PROFILES` from the
old layout is kept for compatibility but ignored.

## RAW HID fallback detection

The widget treats RAW HID as "not ready" until it receives any packet from
the host. After 65 s of silence (`disconnect_timer` in [`src/hid.c`](src/hid.c))
it falls back automatically. While the fallback is active the central screen
shows the two-line `Connect / RAW HID` prompt from `references/No RAWHID.svg`
and the peripheral mirrors the same message in its media slots.

## Media text

`NICE_VIEW_HID_TEXT_MAX_LEN` (in [`include/nice_view_hid/hid.h`](include/nice_view_hid/hid.h))
is set to **64** characters per field. Strings are always null-terminated.

Two protocol limits stack on top of that buffer:

1. **Per packet:** Raw HID reports are bounded by `CONFIG_RAW_HID_REPORT_SIZE`
   (default 32). With one type byte and one length byte that leaves **30
   chars** of payload per packet on USB.
2. **Split peripheral:** Forwarding to the peripheral uses a single ATT
   write, so the effective limit drops to **~16 chars** at the default
   23-byte BLE MTU.

To support longer titles/artists the firmware accepts continuation packets:

| Type byte | Meaning |
|-----------|---------|
| `0xAD` `_MEDIA_ARTIST` | Reset artist buffer, append payload |
| `0xAE` `_MEDIA_TITLE`  | Reset title buffer, append payload |
| `0xAF` `_MEDIA_ARTIST_CONT` | Append payload to current artist |
| `0xB0` `_MEDIA_TITLE_CONT`  | Append payload to current title |

Companion apps that don't know about the continuation types still work —
they just send the start packet and the firmware shows whatever fit. Long
titles scroll horizontally on the peripheral via a character-window marquee
(throttled by `CONFIG_NICE_VIEW_HID_MEDIA_SCROLL_INTERVAL_MS` and paused
under `CONFIG_NICE_VIEW_HID_MEDIA_SCROLL_MIN_BATTERY` to spare the battery).

## Asset regeneration

The Bluetooth, USB and "searching" glyphs are inlined as
`LV_COLOR_FORMAT_I1` byte arrays at the top of [`src/widgets/util.c`](src/widgets/util.c)
together with the profile dots (`selected_profile`, `bonded_profile`,
`free_profile`, `selected_free_profile`), the language icon, and the
speaker/volume waveforms.

To re-export from SVG:

1. Open the source SVG in [LVGL's online image converter](https://lvgl.io/tools/imageconverter)
   (or `lv_img_conv`).
2. Pick **`I1` indexed (1-bit, palette-prefixed)** as the colour format —
   the existing arrays have the 8-byte `BG, FG` palette prefix at the top
   followed by the row bytes; matching that lets the `DEFINE_I1_IMG` macro
   wrap them with the right `header.stride` automatically.
3. Replace the array body in `util.c`. Keep the `ELEMENTAL_*_BYTES` palette
   prefix so colours follow `CONFIG_NICE_VIEW_HID_INVERTED`.

The original 1-bit alpha exports in [`references/`](../references/) (e.g.
`Bounded_profile.c`, `Language_icon.c`) were the inputs for the current
build — the only transformation was adding the v9 palette prefix and
re-wrapping with `lv_image_dsc_t` instead of `lv_img_dsc_t`.

## Installation

Add the module to your config's `west.yml` alongside `zmk-raw-hid`:

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: oleksandrmaslov
      url-base: https://github.com/oleksandrmaslov
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-nice-view-hid
      remote: oleksandrmaslov
      revision: codex/nice-view-hid-ui-rework
    - name: zmk-raw-hid
      remote: oleksandrmaslov
      revision: main
  self:
    path: config
```

Add `nice_view_hid_adapter` and `raw_hid_adapter` to the relevant shields in
your `build.yaml`:

```yaml
include:
  - board: nice_nano_v2
    shield: corne_left nice_view_adapter nice_view_hid_adapter raw_hid_adapter
  - board: nice_nano_v2
    shield: corne_right nice_view_adapter nice_view_hid_adapter raw_hid_adapter
```

## Configuration

| Name | Description | Default |
|------|-------------|---------|
| `CONFIG_NICE_VIEW_HID` | Enable the widget | `n` |
| `CONFIG_NICE_VIEW_HID_TWO_PROFILES` | (legacy, ignored — 5 profile dots are always shown) | `n` |
| `CONFIG_NICE_VIEW_HID_SHOW_LAYOUT` | Show language/layout | `y` |
| `CONFIG_NICE_VIEW_HID_LAYOUTS` | Comma-separated layout list | `EN` |
| `CONFIG_NICE_VIEW_HID_INVERTED` | Invert colours | `n` |
| `CONFIG_NICE_VIEW_HID_MEDIA_SCROLL` | Marquee long media text on the peripheral | `y` |
| `CONFIG_NICE_VIEW_HID_MEDIA_SCROLL_INTERVAL_MS` | Marquee tick (ms) | `350` |
| `CONFIG_NICE_VIEW_HID_MEDIA_SCROLL_MIN_BATTERY` | Pause marquee below this battery percentage (unless USB-powered) | `25` |

## Attribution

- Battery and BLE/USB indicator pixel maps are adapted from
  [`kevinpastor/nice-view-elemental`](https://github.com/kevinpastor/nice-view-elemental)
  (MIT, © 2024 Kevin Pastor). The lightning-bolt charging glyph is the same
  pixel pattern, rotated 90° to fit the horizontal battery body.
- Profile, language and volume icons were exported from the SVG sources in
  [`references/`](../references/) and rewrapped for LVGL v9.
