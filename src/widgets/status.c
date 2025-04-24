/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include "status.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#ifdef CONFIG_RAW_HID
#include <nice_view_hid/hid.h>
#endif

// Media widget only on PERIPHERAL
#if !defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) && defined(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
#define NOWPLAY_Y_OFFSET 20
#define NOWPLAY_SCROLL_SPEED 10 //scroll speed in px/s
// forward declarations
static void title_update_cb(struct media_title_notification notif);
static void artist_update_cb(struct media_artist_notification notif);
static void media_conn_update_cb(struct is_connected_notification conn);
#endif

enum widget_children {
    WIDGET_TOP = 0,
    WIDGET_HID,
    WIDGET_MIDDLE,
    WIDGET_BOTTOM,
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
};

struct layer_status_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

#if !defined(CONFIG_NICE_VIEW_HID_MEDIA_INFO)

// ---- All draw_* and set_* functions and their update callbacks ----

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_TOP);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, state);

    // Draw output status
    char output_text[10] = {};

    switch (state->selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        strcat(output_text, LV_SYMBOL_USB);
        break;
    case ZMK_TRANSPORT_BLE:
        if (state->active_profile_bonded) {
            if (state->active_profile_connected) {
                strcat(output_text, LV_SYMBOL_WIFI);
            } else {
                strcat(output_text, LV_SYMBOL_CLOSE);
            }
        } else {
            strcat(output_text, LV_SYMBOL_SETTINGS);
        }
        break;
    }

    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, output_text);

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void draw_hid(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_HID);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_label_dsc_t label_time;
    init_label_dsc(&label_time, LVGL_FOREGROUND, &lv_font_montserrat_22, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_layout;
    init_label_dsc(&label_layout, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_volume;
    init_label_dsc(&label_volume, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

#ifdef CONFIG_RAW_HID
#define TEXT_OFFSET_Y (IS_ENABLED(CONFIG_NICE_VIEW_HID_SHOW_LAYOUT) ? 0 : 8)
    if (state->is_connected) {
        // Draw hid data
        char time[10] = {};
        sprintf(time, "%02i:%02i", state->hour, state->minute);
        lv_canvas_draw_text(canvas, 0, 0 + TEXT_OFFSET_Y, 68, &label_time, time);

        char layout[10] = {};
#ifdef CONFIG_NICE_VIEW_HID_SHOW_LAYOUT
        char layouts[sizeof(CONFIG_NICE_VIEW_HID_LAYOUTS)];
        strcpy(layouts, CONFIG_NICE_VIEW_HID_LAYOUTS);
        char *current_layout = strtok(layouts, ",");
        size_t i = 0;
        while (current_layout != NULL && i < state->layout) {
            i++;
            current_layout = strtok(NULL, ",");
        }

        if (current_layout != NULL) {
            sprintf(layout, "%s", current_layout);
        } else {
            sprintf(layout, "%i", state->layout);
        }

#endif
        lv_canvas_draw_text(canvas, 0, 27, 68, &label_layout, layout);

        char volume[10] = {};
        sprintf(volume, "vol: %i", state->volume);
#if defined(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
// skip drawing volume when media is shown
#else
        lv_canvas_draw_text(canvas, 0, 50 - TEXT_OFFSET_Y, 68, &label_volume, volume);
#endif
    } else
#endif
    {
        lv_canvas_draw_text(canvas, 0, 0, 68, &label_time, "HID");
        lv_canvas_draw_text(canvas, 0, 27, 68, &label_layout, "not");
        lv_canvas_draw_text(canvas, 0, 50, 68, &label_volume, "found");
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void draw_middle(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_MIDDLE);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);
    lv_draw_arc_dsc_t arc_dsc;
    init_arc_dsc(&arc_dsc, LVGL_FOREGROUND, 2);
    lv_draw_arc_dsc_t arc_dsc_filled;
    init_arc_dsc(&arc_dsc_filled, LVGL_FOREGROUND, 9);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_dsc_black;
    init_label_dsc(&label_dsc_black, LVGL_BACKGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw circles
#define ARC_OFFSET_Y 12
#ifdef CONFIG_NICE_VIEW_HID_TWO_PROFILES
    int circle_offsets[2][2] = {{17, 13 + ARC_OFFSET_Y}, {51, 13 + ARC_OFFSET_Y}};

    for (int i = 0; i < 2; i++) {
        bool selected = i == state->active_profile_index;

        lv_canvas_draw_arc(canvas, circle_offsets[i][0], circle_offsets[i][1], 13, 0, 360,
                           &arc_dsc);

        if (selected) {
            lv_canvas_draw_arc(canvas, circle_offsets[i][0], circle_offsets[i][1], 9, 0, 359,
                               &arc_dsc_filled);
        }

        char label[2];
        snprintf(label, sizeof(label), "%d", i + 1);
        lv_canvas_draw_text(canvas, circle_offsets[i][0] - 8, circle_offsets[i][1] - 10, 16,
                            (selected ? &label_dsc_black : &label_dsc), label);
    }
#else
    lv_canvas_draw_arc(canvas, 34, 13 + ARC_OFFSET_Y, 13, 0, 360, &arc_dsc);

    char label[4];
    snprintf(label, sizeof(label), "%i", state->active_profile_index + 1);
    lv_canvas_draw_text(canvas, 26, 3 + ARC_OFFSET_Y, 16, &label_dsc, label);
#endif

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void draw_bottom(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_BOTTOM);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

#if defined(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
// skip drawing layer when media is shown
#else
    // Draw layer
    if (state->layer_label == NULL || strlen(state->layer_label) == 0) {
        char text[10] = {};
        sprintf(text, "LAYER %i", state->layer_index);
        lv_canvas_draw_text(canvas, 0, 5, 68, &label_dsc, text);
    } else {
        lv_canvas_draw_text(canvas, 0, 5, 68, &label_dsc, state->layer_label);
    }
#endif

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif
    widget->state.battery = state.level;
    draw_top(widget->obj, widget->cbuf, &widget->state);
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
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

static void set_output_status(struct zmk_widget_status *widget,
                              const struct output_status_state *state) {
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_index = state->active_profile_index;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;

    draw_top(widget->obj, widget->cbuf, &widget->state);
    draw_middle(widget->obj, widget->cbuf2, &widget->state);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_output_status(widget, &state); }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    return (struct output_status_state){
        .selected_endpoint = zmk_endpoints_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

static void set_layer_status(struct zmk_widget_status *widget, struct layer_status_state state) {
    widget->state.layer_index = state.index;
    widget->state.layer_label = state.label;
    draw_bottom(widget->obj, widget->cbuf3, &widget->state);
}

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_layer_status(widget, state); }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

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

        draw_hid(widget->obj, widget->cbuf_hid, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_is_connected, struct is_connected_notification,
                            is_hid_connected_update_cb, get_is_hid_connected)
ZMK_SUBSCRIPTION(widget_is_connected, is_connected_notification);

static struct time_notification get_time(const zmk_event_t *eh) {
    struct time_notification *notification = as_time_notification(eh);
    if (notification) {
        return *notification;
    }
    return (struct time_notification){.hour = 0, .minute = 0};
}

static void time_update_cb(struct time_notification time) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.hour = time.hour;
        widget->state.minute = time.minute;

        draw_hid(widget->obj, widget->cbuf_hid, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_time, struct time_notification, time_update_cb, get_time)
ZMK_SUBSCRIPTION(widget_time, time_notification);

static struct volume_notification get_volume(const zmk_event_t *eh) {
    struct volume_notification *notification = as_volume_notification(eh);
    if (notification) {
        return *notification;
    }
    return (struct volume_notification){.value = 0};
}

static void volume_update_cb(struct volume_notification volume) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.volume = volume.value;

        draw_hid(widget->obj, widget->cbuf_hid, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_volume, struct volume_notification, volume_update_cb, get_volume)
ZMK_SUBSCRIPTION(widget_volume, volume_notification);

#ifdef CONFIG_NICE_VIEW_HID_SHOW_LAYOUT

static struct layout_notification get_layout(const zmk_event_t *eh) {
    struct layout_notification *notification = as_layout_notification(eh);
    if (notification) {
        return *notification;
    }
    return (struct layout_notification){.value = 0};
}

static void layout_update_cb(struct layout_notification layout) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.layout = layout.value;

        draw_hid(widget->obj, widget->cbuf_hid, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layout, struct layout_notification, layout_update_cb, get_layout)
ZMK_SUBSCRIPTION(widget_layout, layout_notification);

#endif

#if !defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) && defined(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
static struct media_title_notification get_title_notif(const zmk_event_t *eh) {
    struct media_title_notification *ev = as_media_title_notification(eh);
    return ev ? *ev : (struct media_title_notification){ .title = "" };
}

static void title_update_cb(struct media_title_notification notif) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        strncpy(widget->state.track_title, notif.title, sizeof(widget->state.track_title));
        widget->state.track_title[sizeof(widget->state.track_title)-1] = '\0';
        if (widget->state.track_title[0] == '\0') {
            lv_label_set_text(widget->label_track, "No media");
            lv_label_set_text(widget->label_artist, "");
        } else {
            lv_label_set_text(widget->label_track, widget->state.track_title);
        }
    }
}

static struct media_artist_notification get_artist_notif(const zmk_event_t *eh) {
    struct media_artist_notification *ev = as_media_artist_notification(eh);
    return ev ? *ev : (struct media_artist_notification){ .artist = "" };
}

static void artist_update_cb(struct media_artist_notification notif) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        if (widget->state.track_title[0] != '\0') {
            strncpy(widget->state.track_artist, notif.artist, sizeof(widget->state.track_artist));
            widget->state.track_artist[sizeof(widget->state.track_artist)-1] = '\0';
            lv_label_set_text(widget->label_artist, widget->state.track_artist);
        }
    }
}

static void media_conn_update_cb(struct is_connected_notification conn) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        if (!conn.value) {
            widget->state.track_title[0] = '\0';
            widget->state.track_artist[0] = '\0';
            lv_label_set_text(widget->label_track, "No media");
            lv_label_set_text(widget->label_artist, "");
        }
    }
}

// Register listeners
ZMK_DISPLAY_WIDGET_LISTENER(widget_media_title, struct media_title_notification,
                             title_update_cb, get_title_notif)
ZMK_SUBSCRIPTION(widget_media_title, media_title_notification);

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_artist, struct media_artist_notification,
                             artist_update_cb, get_artist_notif)
ZMK_SUBSCRIPTION(widget_media_artist, media_artist_notification);

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_conn, struct is_connected_notification,
                             media_conn_update_cb, get_is_hid_connected)
ZMK_SUBSCRIPTION(widget_media_conn, is_connected_notification);
#endif // peripheral media widget

#endif // CONFIG_RAW_HID

#endif // !defined(CONFIG_NICE_VIEW_HID_MEDIA_INFO)

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);

    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    lv_obj_t *hid = lv_canvas_create(widget->obj);
    lv_obj_align(hid, LV_ALIGN_TOP_LEFT, 64, 0);
    lv_canvas_set_buffer(hid, widget->cbuf_hid, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    lv_obj_t *middle = lv_canvas_create(widget->obj);
    lv_obj_align(middle, LV_ALIGN_TOP_LEFT, -4, 0);
    lv_canvas_set_buffer(middle, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    lv_obj_t *bottom = lv_canvas_create(widget->obj);
    lv_obj_align(bottom, LV_ALIGN_TOP_LEFT, -44, 0);
    lv_canvas_set_buffer(bottom, widget->cbuf3, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    // Ensure state is zero-initialized (no stale data)
    memset(&widget->state, 0, sizeof(widget->state));

#if !defined(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
    // Only register the old listeners when not in media-info mode
    widget_battery_status_init();
    widget_output_status_init();
    widget_layer_status_init();
#ifdef CONFIG_RAW_HID
    widget_is_connected_init();
    widget_time_init();
    widget_volume_init();
#ifdef CONFIG_NICE_VIEW_HID_SHOW_LAYOUT
    widget_layout_init();
#endif
#endif // !CONFIG_NICE_VIEW_HID_MEDIA_INFO

#if defined(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
    // Now Playing header
    widget->label_now = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->label_now, &lv_font_montserrat_12, 0);
    lv_label_set_text_static(widget->label_now, "Now Playing");
    lv_obj_set_pos(widget->label_now, 0, NOWPLAY_Y_OFFSET);

    // Track title (scrolling)
    widget->label_track = lv_label_create(widget->obj);
    lv_obj_set_width(widget->label_track, 160);
    lv_obj_set_style_text_font(widget->label_track, &lv_font_montserrat_18, 0);
    lv_label_set_long_mode(widget->label_track, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_speed(widget->label_track, NOWPLAY_SCROLL_SPEED, 0);
    lv_label_set_text(widget->label_track, "No media");
    lv_obj_set_pos(widget->label_track, 0, NOWPLAY_Y_OFFSET + 12 + 4);

    // Artist name
    widget->label_artist = lv_label_create(widget->obj);
    lv_obj_set_width(widget->label_artist, 160);
    lv_obj_set_style_text_font(widget->label_artist, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(widget->label_artist, LV_LABEL_LONG_DOT);
    lv_label_set_text(widget->label_artist, "");
    lv_obj_set_pos(widget->label_artist, 0, NOWPLAY_Y_OFFSET + 12 + 4 + 18 + 2);

    // Register your media listeners
    widget_media_title_init();
    widget_media_artist_init();
    widget_media_conn_init();
#endif

    sys_slist_append(&widgets, &widget->node);

#if !defined(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
    // Draw the normal widgets on init if not in media mode
    draw_hid(widget->obj, widget->cbuf_hid, &widget->state);
#endif

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
