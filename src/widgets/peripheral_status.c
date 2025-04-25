/*
 * peripheral_status.c
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>

#ifdef CONFIG_NICE_VIEW_HID_MEDIA_INFO
#include <nice_view_hid/hid.h>
#endif

#include "peripheral_status.h"
#include <raw_hid/events.h>

// -----------------------------------------------------------------------------
// 0) RAW_HID: handle central commands
// -----------------------------------------------------------------------------
int zmk_split_transport_peripheral_command_handler(
    const struct zmk_split_transport_peripheral *transport,
    struct zmk_split_transport_central_command cmd) {
    ARG_UNUSED(transport);
    if (cmd.type == ZMK_SPLIT_TRANSPORT_CENTRAL_CMD_TYPE_RAW_HID) {
        struct raw_hid_received_event evt = {
            .data   = (uint8_t *)cmd.data.raw_hid.data,
            .length = sizeof(cmd.data.raw_hid.data),
        };
        raise_raw_hid_received_event(evt);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// 1) DRAW UTILITY: top bar drawing
// -----------------------------------------------------------------------------
static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct peripheral_status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    /* Fill background */
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    /* Draw connection icon */
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                        state->connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    /* Rotate canvas for display orientation */
    rotate_canvas(canvas, cbuf);
}

// -----------------------------------------------------------------------------
// 2) CONNECTION ICON & WIDGET LIST
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){
        .connected = zmk_split_bt_peripheral_is_connected()
    };
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        w->state.connected = state.connected;
        draw_top(w->obj, w->cbuf, &w->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status,
    struct peripheral_status_state, output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

// -----------------------------------------------------------------------------
// 2) NOW PLAYING (MEDIA INFO)
// -----------------------------------------------------------------------------
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_INFO)

static struct media_title_notification get_title_notif(const zmk_event_t *eh) {
    struct media_title_notification *ev = as_media_title_notification(eh);
    return ev ? *ev : (struct media_title_notification){ .title = "" };
}

static void title_update_cb(struct media_title_notification notif) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        strncpy(w->state.track_title, notif.title, sizeof(w->state.track_title));
        w->state.track_title[sizeof(w->state.track_title)-1] = '\0';
        lv_label_set_text(w->label_track,
            w->state.track_title[0] ? w->state.track_title : "No media");
    }
}
ZMK_DISPLAY_WIDGET_LISTENER(widget_media_title,
    struct media_title_notification, title_update_cb, get_title_notif)
ZMK_SUBSCRIPTION(widget_media_title, media_title_notification);

static struct media_artist_notification get_artist_notif(const zmk_event_t *eh) {
    struct media_artist_notification *ev = as_media_artist_notification(eh);
    return ev ? *ev : (struct media_artist_notification){ .artist = "" };
}

static void artist_update_cb(struct media_artist_notification notif) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        if (w->state.track_title[0]) {
            strncpy(w->state.track_artist, notif.artist, sizeof(w->state.track_artist));
            w->state.track_artist[sizeof(w->state.track_artist)-1] = '\0';
            lv_label_set_text(w->label_artist, w->state.track_artist);
        }
    }
}
ZMK_DISPLAY_WIDGET_LISTENER(widget_media_artist,
    struct media_artist_notification, artist_update_cb, get_artist_notif)
ZMK_SUBSCRIPTION(widget_media_artist, media_artist_notification);

static struct is_connected_notification get_media_conn_notif(const zmk_event_t *eh) {
    struct is_connected_notification *ev = as_is_connected_notification(eh);
    return ev ? *ev : (struct is_connected_notification){ .value = false };
}

static void media_conn_update_cb(struct is_connected_notification notif) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        lv_label_set_text(w->label_now, notif.value ? "Now Playing" : "");
    }
}
ZMK_DISPLAY_WIDGET_LISTENER(widget_media_conn,
    struct is_connected_notification, media_conn_update_cb, get_media_conn_notif)
ZMK_SUBSCRIPTION(widget_media_conn, is_connected_notification);

#endif // CONFIG_NICE_VIEW_HID_MEDIA_INFO

// -----------------------------------------------------------------------------
// 3) WIDGET INIT
// -----------------------------------------------------------------------------
int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);

    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_peripheral_status_init();

#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
    widget_media_conn_init();
    widget_media_title_init();
    widget_media_artist_init();

    /* "Now Playing" header */
    widget->label_now = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->label_now, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_label_set_text_static(widget->label_now, "");
    lv_obj_set_pos(widget->label_now, 0, 32);

    /* Track title (scrolling) */
    widget->label_track = lv_label_create(widget->obj);
    lv_obj_set_width(widget->label_track, CANVAS_SIZE);
    lv_obj_set_style_text_font(widget->label_track, &lv_font_montserrat_18, LV_STATE_DEFAULT);
    lv_label_set_long_mode(widget->label_track, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(widget->label_track, "");
    lv_obj_set_pos(widget->label_track, 0, 44);

    /* Artist name (truncated) */
    widget->label_artist = lv_label_create(widget->obj);
    lv_obj_set_width(widget->label_artist, CANVAS_SIZE);
    lv_obj_set_style_text_font(widget->label_artist, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_label_set_long_mode(widget->label_artist, LV_LABEL_LONG_DOT);
    lv_label_set_text(widget->label_artist, "");
    lv_obj_set_pos(widget->label_artist, 0, 56);
#endif

    // initial rendering
    struct peripheral_status_state st = get_state(NULL);
    draw_top(widget->obj, widget->cbuf, &st);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) {
    return widget->obj;
}
