/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "peripheral_status.h"

/*
 * Peripheral layout — flat horizontal on the 160×68 display:
 *
 *   y=  ┌─────────────────────────────────────────────────────────────┐
 *    0  │ [BAT 33×12]                                       [BT]      │
 *   16  ├─────────────────────────────────────────────────────────────┤
 *   22  │ Bohemian Rhapsody                          (marquee 18-px)  │
 *   42  │ Queen                                      (marquee 14-px)  │
 *       └─────────────────────────────────────────────────────────────┘
 *
 * Long titles scroll character-by-character. Battery rendering, marquee
 * pacing, and battery-aware pause are all in util.c / Kconfig.
 */

#define MEDIA_TEXT_X 4
#define MEDIA_TEXT_RIGHT 156
#define MEDIA_AXIAL_LENGTH (MEDIA_TEXT_RIGHT - MEDIA_TEXT_X)
#define MEDIA_TITLE_Y 20
#define MEDIA_ARTIST_Y 44

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

#if IS_ENABLED(CONFIG_RAW_HID) && IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_SCROLL)
static lv_timer_t *media_scroll_timer;
static uint16_t media_scroll_step;
#endif

static void fill_canvas(lv_obj_t *canvas) {
    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);
}

static void draw_header(lv_obj_t *canvas, const struct status_state *state) {
    draw_battery(canvas, state);

    if (state->connected) {
        draw_elemental_bluetooth_logo(canvas, 146, 0);
    } else {
        draw_elemental_bluetooth_logo_outlined(canvas, 146, 0);
    }
}

static const char *fallback_title(const struct status_state *state) {
#if IS_ENABLED(CONFIG_RAW_HID)
    if (!state->connected) return "Waiting link";
    if (!state->is_connected) return "Connect RAW HID";
    return state->media_title[0] ? state->media_title : "Now playing";
#else
    return state->connected ? "Connected" : "Disconnected";
#endif
}

static const char *fallback_artist(const struct status_state *state) {
#if IS_ENABLED(CONFIG_RAW_HID)
    if (!state->connected) return "Split offline";
    if (!state->is_connected) return "Waiting host";
    return state->media_artist[0] ? state->media_artist : "";
#else
    ARG_UNUSED(state);
    return "";
#endif
}

#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_SCROLL)
static size_t utf8_char_advance(const char *s) {
    if (s == NULL || *s == '\0') return 0;
    uint8_t b = (uint8_t)*s;
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

static size_t utf8_strlen(const char *s) {
    size_t n = 0;
    while (s != NULL && *s) {
        s += utf8_char_advance(s);
        n++;
    }
    return n;
}

static const char *utf8_advance_chars(const char *s, size_t chars) {
    while (chars > 0 && s != NULL && *s) {
        s += utf8_char_advance(s);
        chars--;
    }
    return s;
}

static lv_coord_t measure_text_width(const char *txt, const lv_font_t *font) {
    lv_point_t size;
    lv_text_get_size(&size, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}
#endif

static bool scroll_allowed(const struct status_state *state) {
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_SCROLL)
    if (state->battery >= CONFIG_NICE_VIEW_HID_MEDIA_SCROLL_MIN_BATTERY) return true;
    return state->charging;
#else
    ARG_UNUSED(state);
    return false;
#endif
}

/*
 * Draw a horizontal text label at (x, y) using the given font, applying a
 * character-window marquee when the text is wider than the available area.
 * No rotation — text reads naturally left-to-right.
 */
static void draw_marquee_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                              const lv_font_t *font, lv_color_t color, const char *txt,
                              uint16_t step, bool may_scroll) {
    lv_draw_label_dsc_t dsc;
    init_label_dsc(&dsc, color, font, LV_TEXT_ALIGN_LEFT);
    dsc.flag |= LV_TEXT_FLAG_EXPAND; /* belt-and-braces: never wrap */

#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_SCROLL)
    lv_coord_t full_w = measure_text_width(txt, font);
    if (!may_scroll || full_w <= max_w) {
        canvas_draw_text(canvas, x, y, max_w, &dsc, txt);
        return;
    }

    size_t total_chars = utf8_strlen(txt);
    /*
     * Pause briefly at the start (steps 0..2) then advance the window one
     * character per tick. After the whole title has scrolled past, hold
     * empty for 3 ticks and restart. CHAR-LEVEL is intentionally cheap on
     * battery — see CONFIG_NICE_VIEW_HID_MEDIA_SCROLL_INTERVAL_MS.
     */
    size_t cycle = total_chars + 6;
    size_t phase = step % cycle;
    size_t skip = (phase < 3) ? 0 : (phase - 3);
    if (skip > total_chars) skip = total_chars;

    const char *windowed = utf8_advance_chars(txt, skip);
    canvas_draw_text(canvas, x, y, max_w, &dsc, windowed);
#else
    ARG_UNUSED(step);
    ARG_UNUSED(may_scroll);
    canvas_draw_text(canvas, x, y, max_w, &dsc, txt);
#endif
}

static void draw_media(lv_obj_t *canvas, const struct status_state *state, uint16_t step) {
    const char *title = fallback_title(state);
    const char *artist = fallback_artist(state);
    bool may_scroll = scroll_allowed(state);

    draw_marquee_text(canvas, MEDIA_TEXT_X, MEDIA_TITLE_Y, MEDIA_AXIAL_LENGTH,
                      &lv_font_montserrat_18, LVGL_FOREGROUND, title, step, may_scroll);
    draw_marquee_text(canvas, MEDIA_TEXT_X, MEDIA_ARTIST_Y, MEDIA_AXIAL_LENGTH,
                      &lv_font_montserrat_14, LVGL_FOREGROUND, artist, step, may_scroll);
}

static void redraw_widget(struct zmk_widget_status *widget) {
    fill_canvas(widget->screen_canvas);
    draw_header(widget->screen_canvas, &widget->state);
#if IS_ENABLED(CONFIG_RAW_HID) && IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_SCROLL)
    draw_media(widget->screen_canvas, &widget->state, media_scroll_step);
#else
    draw_media(widget->screen_canvas, &widget->state, 0);
#endif
    lv_obj_invalidate(widget->screen_canvas);
}

#if IS_ENABLED(CONFIG_RAW_HID) && IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_SCROLL)
static bool any_widget_needs_scroll(void) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        if (!w->state.connected || !w->state.is_connected) continue;
        if (!scroll_allowed(&w->state)) continue;
        if (w->state.media_title[0] == '\0' && w->state.media_artist[0] == '\0') continue;
        lv_coord_t t = measure_text_width(
            w->state.media_title[0] ? w->state.media_title : "Now playing",
            &lv_font_montserrat_18);
        lv_coord_t a = measure_text_width(
            w->state.media_artist[0] ? w->state.media_artist : " ", &lv_font_montserrat_14);
        if (t > MEDIA_AXIAL_LENGTH || a > MEDIA_AXIAL_LENGTH) return true;
    }
    return false;
}

static void media_scroll_tick(lv_timer_t *t) {
    ARG_UNUSED(t);
    if (!any_widget_needs_scroll()) {
        media_scroll_step = 0;
        return;
    }
    media_scroll_step++;
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) { redraw_widget(w); }
}
#endif

static void copy_text_field(char *dst, const char *src) {
#if IS_ENABLED(CONFIG_RAW_HID)
    strncpy(dst, src, NICE_VIEW_HID_TEXT_MAX_LEN);
    dst[NICE_VIEW_HID_TEXT_MAX_LEN] = '\0';
#else
    ARG_UNUSED(dst);
    ARG_UNUSED(src);
#endif
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif
    widget->state.battery = state.level;
    redraw_widget(widget);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_status *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;
    redraw_widget(widget);
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

#ifdef CONFIG_RAW_HID

static struct is_connected_notification get_is_hid_connected(const zmk_event_t *eh) {
    struct is_connected_notification *n = as_is_connected_notification(eh);
    return n ? *n : (struct is_connected_notification){.value = false};
}

static void is_hid_connected_update_cb(struct is_connected_notification is_connected) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.is_connected = is_connected.value;
        if (!is_connected.value) {
            widget->state.media_artist[0] = '\0';
            widget->state.media_title[0] = '\0';
        }
        redraw_widget(widget);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_is_connected, struct is_connected_notification,
                            is_hid_connected_update_cb, get_is_hid_connected)
ZMK_SUBSCRIPTION(widget_is_connected, is_connected_notification);

static struct media_title_notification get_media_title(const zmk_event_t *eh) {
    struct media_title_notification *n = as_media_title_notification(eh);
    return n ? *n : (struct media_title_notification){0};
}

static void media_title_update_cb(struct media_title_notification title) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        copy_text_field(widget->state.media_title, title.value);
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_SCROLL)
        media_scroll_step = 0;
#endif
        redraw_widget(widget);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_title, struct media_title_notification,
                            media_title_update_cb, get_media_title)
ZMK_SUBSCRIPTION(widget_media_title, media_title_notification);

static struct media_artist_notification get_media_artist(const zmk_event_t *eh) {
    struct media_artist_notification *n = as_media_artist_notification(eh);
    return n ? *n : (struct media_artist_notification){0};
}

static void media_artist_update_cb(struct media_artist_notification artist) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        copy_text_field(widget->state.media_artist, artist.value);
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_SCROLL)
        media_scroll_step = 0;
#endif
        redraw_widget(widget);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_artist, struct media_artist_notification,
                            media_artist_update_cb, get_media_artist)
ZMK_SUBSCRIPTION(widget_media_artist, media_artist_notification);

#endif /* CONFIG_RAW_HID */

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, NICE_VIEW_HID_SCREEN_WIDTH, NICE_VIEW_HID_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(widget->obj, LVGL_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(widget->obj, 0, 0);
    lv_obj_set_style_pad_all(widget->obj, 0, 0);
    memset(&widget->state, 0, sizeof(widget->state));

    widget->screen_canvas = lv_canvas_create(widget->obj);
    lv_obj_align(widget->screen_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_set_buffer(widget->screen_canvas, widget->screen_cbuf, NICE_VIEW_HID_SCREEN_WIDTH,
                         NICE_VIEW_HID_SCREEN_HEIGHT, CANVAS_COLOR_FORMAT);

    fill_canvas(widget->screen_canvas);
    lv_obj_invalidate(widget->screen_canvas);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();
#ifdef CONFIG_RAW_HID
    widget_is_connected_init();
    widget_media_title_init();
    widget_media_artist_init();
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_SCROLL)
    if (media_scroll_timer == NULL) {
        media_scroll_timer = lv_timer_create(media_scroll_tick,
                                             CONFIG_NICE_VIEW_HID_MEDIA_SCROLL_INTERVAL_MS, NULL);
    }
#endif
#endif

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
