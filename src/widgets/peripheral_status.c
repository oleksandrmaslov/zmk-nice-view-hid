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
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "peripheral_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

static bool raw_hid_ready(const struct status_state *state) {
#if IS_ENABLED(CONFIG_RAW_HID)
    return state->is_connected;
#else
    ARG_UNUSED(state);
    return false;
#endif
}

static void draw_peripheral_media(lv_obj_t *canvas, const struct status_state *state) {
    if (!state->connected) {
        draw_text(canvas, 4, 28, 60, 18, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER,
                  "Peripheral");
        draw_text(canvas, 4, 50, 60, 14, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
                  "Waiting link");
        draw_text(canvas, 4, 68, 60, 12, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
                  "Split offline");
        return;
    }

    if (!raw_hid_ready(state)) {
        draw_text(canvas, 4, 34, 60, 14, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
                  "Connect");
        draw_text(canvas, 4, 51, 60, 24, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER,
                  "RAW HID");
        return;
    }

    draw_text(canvas, 4, 28, 60, 18, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER,
              state->media_title[0] != '\0' ? state->media_title : "No title");
    draw_text(canvas, 4, 50, 60, 14, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
              state->media_artist[0] != '\0' ? state->media_artist : "No artist");
    draw_text(canvas, 4, 68, 60, 12, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER, "Playing");
    draw_play_icon(canvas, 56, 29);
}

static void refresh_widget(struct zmk_widget_status *widget) {
    lv_obj_t *canvas = widget->portrait_canvas;

    draw_status_background(canvas);
    draw_battery(canvas, 4, 6, &widget->state);
    draw_ble_icon(canvas, 52, 4, true, widget->state.connected);
    draw_peripheral_media(canvas, &widget->state);

    rotate_portrait_canvas(widget->portrait_canvas, widget->display_canvas);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif
    widget->state.battery = state.level;

    refresh_widget(widget);
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
    refresh_widget(widget);
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
    struct is_connected_notification *notification = as_is_connected_notification(eh);
    if (notification) {
        return *notification;
    }
    return (struct is_connected_notification){.value = false};
}

static void is_hid_connected_update_cb(struct is_connected_notification is_connected) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.is_connected = is_connected.value;
        if (!is_connected.value) {
            widget->state.media_artist[0] = '\0';
            widget->state.media_title[0] = '\0';
        }

        refresh_widget(widget);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_is_connected, struct is_connected_notification,
                            is_hid_connected_update_cb, get_is_hid_connected)
ZMK_SUBSCRIPTION(widget_is_connected, is_connected_notification);

static struct media_title_notification get_media_title(const zmk_event_t *eh) {
    struct media_title_notification *notification = as_media_title_notification(eh);
    if (notification) {
        return *notification;
    }
    return (struct media_title_notification){0};
}

static void media_title_update_cb(struct media_title_notification title) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        copy_text_field(widget->state.media_title, title.value);
        refresh_widget(widget);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_title, struct media_title_notification,
                            media_title_update_cb, get_media_title)
ZMK_SUBSCRIPTION(widget_media_title, media_title_notification);

static struct media_artist_notification get_media_artist(const zmk_event_t *eh) {
    struct media_artist_notification *notification = as_media_artist_notification(eh);
    if (notification) {
        return *notification;
    }
    return (struct media_artist_notification){0};
}

static void media_artist_update_cb(struct media_artist_notification artist) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        copy_text_field(widget->state.media_artist, artist.value);
        refresh_widget(widget);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_artist, struct media_artist_notification,
                            media_artist_update_cb, get_media_artist)
ZMK_SUBSCRIPTION(widget_media_artist, media_artist_notification);

#endif

static void init_canvases(struct zmk_widget_status *widget) {
    widget->display_canvas = lv_canvas_create(widget->obj);
    init_canvas_obj(widget->display_canvas, widget->display_buf, NICE_VIEW_HID_SCREEN_WIDTH,
                    NICE_VIEW_HID_SCREEN_HEIGHT);

    widget->portrait_canvas = lv_canvas_create(widget->obj);
    init_canvas_obj(widget->portrait_canvas, widget->portrait_buf, NICE_VIEW_HID_PORTRAIT_WIDTH,
                    NICE_VIEW_HID_PORTRAIT_HEIGHT);
    lv_obj_add_flag(widget->portrait_canvas, LV_OBJ_FLAG_HIDDEN);
}

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    memset(widget, 0, sizeof(*widget));
    widget->obj = lv_obj_create(parent);
    init_root_obj(widget->obj);

    init_canvases(widget);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();
#ifdef CONFIG_RAW_HID
    widget_is_connected_init();
    widget_media_title_init();
    widget_media_artist_init();
#endif

    refresh_widget(widget);
    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
