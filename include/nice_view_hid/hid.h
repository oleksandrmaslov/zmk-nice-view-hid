#pragma once

#include <zmk/event_manager.h>

#ifdef CONFIG_RAW_HID

/*
 * Maximum length of a media text field (track title or artist) the firmware
 * keeps in RAM and renders. Picked generously so the LVGL marquee always has
 * the whole string to scroll through.
 *
 * Per-packet protocol limit
 * -------------------------
 * The raw-HID packet format is:
 *   data[0] = type byte (e.g. _MEDIA_TITLE)
 *   data[1] = declared payload length
 *   data[2..] = payload bytes
 * Packet size is bounded by CONFIG_RAW_HID_REPORT_SIZE (default 32), so a
 * single packet carries at most 30 chars. On the split peripheral, the BLE
 * MTU shrinks the effective payload further (~16 chars on the default
 * 23-byte MTU). To send anything longer the companion app emits one start
 * packet (_MEDIA_TITLE / _MEDIA_ARTIST) followed by zero or more continuation
 * packets (_MEDIA_TITLE_CONT / _MEDIA_ARTIST_CONT) which the firmware appends
 * up to NICE_VIEW_HID_TEXT_MAX_LEN. Older companion apps that don't speak the
 * continuation protocol still work — they just send a single packet capped at
 * the per-packet limit.
 */
#define NICE_VIEW_HID_TEXT_MAX_LEN 64

struct is_connected_notification {
    bool value;
};

ZMK_EVENT_DECLARE(is_connected_notification);

struct time_notification {
    uint8_t hour;
    uint8_t minute;
};

ZMK_EVENT_DECLARE(time_notification);

struct volume_notification {
    uint8_t value;
};

ZMK_EVENT_DECLARE(volume_notification);

struct media_artist_notification {
    char value[NICE_VIEW_HID_TEXT_MAX_LEN + 1];
};

ZMK_EVENT_DECLARE(media_artist_notification);

struct media_title_notification {
    char value[NICE_VIEW_HID_TEXT_MAX_LEN + 1];
};

ZMK_EVENT_DECLARE(media_title_notification);

#ifdef CONFIG_NICE_VIEW_HID_SHOW_LAYOUT
struct layout_notification {
    uint8_t value;
};

ZMK_EVENT_DECLARE(layout_notification);
#endif
#endif
