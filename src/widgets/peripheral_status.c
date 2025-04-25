/*
 * peripheral_status.c
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>

#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/split/transport/peripheral.h>      /* ZMK_SPLIT_TRANSPORT_PERIPHERAL_REGISTER */
#include <zmk/split/transport/types.h>           /* zmk_split_transport_central_command      */
#include <raw_hid/events.h>                      /* raw_hid_received_event & raiser          */

#ifdef CONFIG_NICE_VIEW_HID_MEDIA_INFO
#include <nice_view_hid/hid.h>
#endif

#include "peripheral_status.h" /* struct status_state, widget indices, CANVAS_SIZE */

/* -----------------------------------------------------------------------------
 * Helpers – forward declarations from status.c (available in the same repo)
 * -------------------------------------------------------------------------- */
extern void init_label_dsc(lv_draw_label_dsc_t *dsc, lv_color_t color,
                           const lv_font_t *font, lv_text_align_t align);
extern void init_rect_dsc(lv_draw_rect_dsc_t *dsc, lv_color_t color);
extern void init_arc_dsc(lv_draw_arc_dsc_t *dsc, lv_color_t color, uint16_t width);
extern void rotate_canvas(lv_obj_t *canvas, lv_color_t buf[]);
extern void draw_battery(lv_obj_t *canvas, const struct status_state *state);

/* -----------------------------------------------------------------------------
 *  Top‑right canvas: battery + connection icon
 * -------------------------------------------------------------------------- */
void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_TOP);

    /* Dark background */
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    /* Battery icon */
    draw_battery(canvas, state);

    /* Connection / transport icon */
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_RIGHT);

    char output_text[10] = "";
    switch (state->selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        strcat(output_text, LV_SYMBOL_USB);
        break;
    case ZMK_TRANSPORT_BLE:
        if (state->active_profile_bonded) {
            strcat(output_text,
                   state->active_profile_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
        } else {
            strcat(output_text, LV_SYMBOL_SETTINGS);
        }
        break;
    default:
        break;
    }
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, output_text);

    /* Rotate the finished canvas for the nice!view orientation */
    rotate_canvas(canvas, cbuf);
}

/* -----------------------------------------------------------------------------
 *  HID canvas: raw‑hid “time / layout / volume” information
 * -------------------------------------------------------------------------- */
void draw_hid(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_HID);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

#if IS_ENABLED(CONFIG_RAW_HID)
    if (state->is_connected) {
        /* Time */
        char time_str[6];
        snprintf(time_str, sizeof(time_str), "%02i:%02i", state->hour, state->minute);

        lv_draw_label_dsc_t label_time;
        init_label_dsc(&label_time, LVGL_FOREGROUND, &lv_font_montserrat_22,
                       LV_TEXT_ALIGN_CENTER);
        lv_canvas_draw_text(canvas, 0, 0, 68, &label_time, time_str);

        /* Layout + volume drawing is omitted for brevity – add your own implementation
           or copy from the original `status.c`. */
    }
#endif
    rotate_canvas(canvas, cbuf);
}

/* -----------------------------------------------------------------------------
 *  Profile circles canvas
 * -------------------------------------------------------------------------- */
void draw_middle(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_MIDDLE);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    /* …profile‑arc drawing copied from status.c – omitted for brevity … */

    rotate_canvas(canvas, cbuf);
}

/* -----------------------------------------------------------------------------
 *  Bottom canvas: layer indicator (fallback when media‑info disabled)
 * -------------------------------------------------------------------------- */
void draw_bottom(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_BOTTOM);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

#ifndef CONFIG_NICE_VIEW_HID_MEDIA_INFO
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

    char text[12];
    if (!state->layer_label || !strlen(state->layer_label)) {
        snprintf(text, sizeof(text), "LAYER %i", state->layer_index);
    } else {
        strncpy(text, state->layer_label, sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
    }
    lv_canvas_draw_text(canvas, 0, 5, CANVAS_SIZE, &label_dsc, text);
#endif

    rotate_canvas(canvas, cbuf);
}

/* -----------------------------------------------------------------------------
 * Global widget list – one declaration only!
 * -------------------------------------------------------------------------- */
static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* ============================================================================
 * 1) RAW‑HID: intercept split‑transport commands and re‑emit as event
 * ========================================================================== */

/* The weak handler provided by ZMK – we call it first so that the normal
   split‑keyboard plumbing still works. */
extern int __zmk_split_transport_peripheral_command_handler(
    const struct zmk_split_transport_peripheral *transport,
    struct zmk_split_transport_central_command cmd);

/* Our interceptor */
static int raw_hid_cmd_interceptor(
    const struct zmk_split_transport_peripheral *transport,
    struct zmk_split_transport_central_command cmd) {

    /* Let the built‑in handler process the command first. */
    int ret = __zmk_split_transport_peripheral_command_handler(transport, cmd);

    /* If it was a raw‑hid packet, translate it into a ZMK event */
    if (cmd.type == ZMK_SPLIT_TRANSPORT_CENTRAL_CMD_TYPE_RAW_HID) {
        struct raw_hid_received_event evt = {
            .data   = cmd.data.raw_hid.data,
            .length = sizeof(cmd.data.raw_hid.data),
        };
        raise_raw_hid_received_event(evt);
    }
    return ret;
}

/* Register our handler */
static const struct zmk_split_transport_peripheral_api periph_transport_api = {
    .central_command = raw_hid_cmd_interceptor,
};
ZMK_SPLIT_TRANSPORT_PERIPHERAL_REGISTER(raw_hid_periph, &periph_transport_api);

/* ============================================================================
 * 2) Battery widget helpers
 * ========================================================================== */
static void set_battery_status(struct zmk_widget_status *w,
                               struct battery_status_state s) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    w->state.charging = s.usb_present;
#endif
    w->state.battery = s.level;
    draw_top(w->obj, w->cbuf, &w->state);
}

static void battery_update_cb(struct battery_status_state s) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) { set_battery_status(w, s); }
}

static struct battery_status_state battery_get_state(const zmk_event_t *e) {
    ARG_UNUSED(e);
    return (struct battery_status_state){
        .level       = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state, battery_update_cb,
                            battery_get_state)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed)
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed)
#endif

/* ============================================================================
 * 3) Peripheral‑connection icon
 * ========================================================================== */
struct peripheral_status_state {
    bool connected;
};

static struct peripheral_status_state conn_get_state(const zmk_event_t *e) {
    ARG_UNUSED(e);
    return (struct peripheral_status_state){ .connected = zmk_split_bt_peripheral_is_connected() };
}

static void conn_update_cb(struct peripheral_status_state st) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        w->state.connected = st.connected;
        draw_top(w->obj, w->cbuf, &w->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            conn_update_cb, conn_get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed)

/* ============================================================================
 * 4) Now‑playing media information (optional)
 * ========================================================================== */
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_INFO)

static struct media_title_notification get_title_notif(const zmk_event_t *e) {
    struct media_title_notification *ev = as_media_title_notification(e);
    return ev ? *ev : (struct media_title_notification){ .title = "" };
}

static void title_update_cb(struct media_title_notification n) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        strncpy(w->state.track_title, n.title, sizeof(w->state.track_title) - 1);
        w->state.track_title[sizeof(w->state.track_title) - 1] = '\0';
        lv_label_set_text(w->label_track,
                          w->state.track_title[0] ? w->state.track_title : "No media");
    }
}

static struct media_artist_notification get_artist_notif(const zmk_event_t *e) {
    struct media_artist_notification *ev = as_media_artist_notification(e);
    return ev ? *ev : (struct media_artist_notification){ .artist = "" };
}

static void artist_update_cb(struct media_artist_notification n) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        if (w->state.track_title[0]) {
            strncpy(w->state.track_artist, n.artist, sizeof(w->state.track_artist) - 1);
            w->state.track_artist[sizeof(w->state.track_artist) - 1] = '\0';
            lv_label_set_text(w->label_artist, w->state.track_artist);
        }
    }
}

static struct is_connected_notification get_media_conn_notif(const zmk_event_t *e) {
    struct is_connected_notification *ev = as_is_connected_notification(e);
    return ev ? *ev : (struct is_connected_notification){ .value = false };
}

static void media_conn_update_cb(struct is_connected_notification n) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        lv_label_set_text(w->label_now, n.value ? "Now Playing" : "");
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_title, struct media_title_notification, title_update_cb,
                            get_title_notif)
ZMK_SUBSCRIPTION(widget_media_title, media_title_notification)

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_artist, struct media_artist_notification, artist_update_cb,
                            get_artist_notif)
ZMK_SUBSCRIPTION(widget_media_artist, media_artist_notification)

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_conn, struct is_connected_notification, media_conn_update_cb,
                            get_media_conn_notif)
ZMK_SUBSCRIPTION(widget_media_conn, is_connected_notification)

#endif /* CONFIG_NICE_VIEW_HID_MEDIA_INFO */

/* ============================================================================
 * 5) Widget initialisation entry‑point
 * ========================================================================== */
int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    /* Root LVGL object */
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);

    /* Top‑right battery / connection canvas */
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    /* Register this instance so the update callbacks can find it */
    sys_slist_append(&widgets, &widget->node);

    /* Initialise the helper listeners */
    widget_battery_status_init();
    widget_peripheral_status_init();

#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
    /* Now‑playing labels */
    widget->label_now    = lv_label_create(widget->obj);
    widget->label_track  = lv_label_create(widget->obj);
    widget->label_artist = lv_label_create(widget->obj);

    /* Position & style them here – code removed for brevity */

    widget_media_conn_init();
    widget_media_title_init();
    widget_media_artist_init();
#endif

    /* First render */
    draw_top(widget->obj, widget->cbuf, &widget->state);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
