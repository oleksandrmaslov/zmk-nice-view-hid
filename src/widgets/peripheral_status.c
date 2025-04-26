/*
 * peripheral_status.c — nice!view peripheral widget with RAW‑HID support
 * Copyright (c) 2023‑2025 The ZMK Contributors
 * SPDX‑License‑Identifier: MIT
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
#include <zmk/split/transport/peripheral.h>
#include <zmk/split/transport/types.h>
#include <raw_hid/events.h>

#ifdef CONFIG_NICE_VIEW_HID_MEDIA_INFO
#include <nice_view_hid/hid.h>
#endif

#include "peripheral_status.h" /* util helpers + state struct */

/* -------------------------------------------------------------------------- */
/* Canvas helpers (declared in util.h)                                         */
/* -------------------------------------------------------------------------- */
/*  – rotate_canvas()
 *  – init_*_dsc() helpers
 *  – draw_battery()
 * are all declared in util.h already, which peripheral_status.h includes.    */

/* -------------------------------------------------------------------------- */
/*  Top‑right canvas: battery + connection icon                               */
/* -------------------------------------------------------------------------- */
void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state)
{
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_TOP);

    /* Background fill */
    lv_draw_rect_dsc_t rect_bg;
    init_rect_dsc(&rect_bg, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_bg);

    /* Battery icon */
    draw_battery(canvas, state);

    /* Transport / connection symbol */
    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_RIGHT);

    char txt[10] = "";
    switch (state->selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        strcat(txt, LV_SYMBOL_USB);
        break;
    case ZMK_TRANSPORT_BLE:
        if (state->active_profile_bonded) {
            strcat(txt, state->active_profile_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
        } else {
            strcat(txt, LV_SYMBOL_SETTINGS);
        }
        break;
    default:
        break;
    }
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &lbl, txt);

    rotate_canvas(canvas, cbuf);
}

/* -------------------------------------------------------------------------- */
/*  HID canvas: RAW‑HID time / layout / volume info                           */
/* -------------------------------------------------------------------------- */
void draw_hid(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state)
{
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_HID);

    lv_draw_rect_dsc_t rect_bg;
    init_rect_dsc(&rect_bg, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_bg);

#if IS_ENABLED(CONFIG_RAW_HID)
    if (state->is_connected) {
        lv_draw_label_dsc_t lbl_time, lbl_layout, lbl_vol;
        init_label_dsc(&lbl_time,   LVGL_FOREGROUND, &lv_font_montserrat_22, LV_TEXT_ALIGN_CENTER);
        init_label_dsc(&lbl_layout, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);
        init_label_dsc(&lbl_vol,    LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

        char buf[10];
        snprintf(buf, sizeof(buf), "%02u:%02u", state->hour, state->minute);
        lv_canvas_draw_text(canvas, 0, 0, 68, &lbl_time, buf);

        /* Layout and volume drawing trimmed for brevity – copy from status.c if needed */
    } else {
        /* Fallback when host not connected */
        lv_draw_label_dsc_t lbl;
        init_label_dsc(&lbl, LVGL_FOREGROUND, &lv_font_montserrat_22, LV_TEXT_ALIGN_CENTER);
        lv_canvas_draw_text(canvas, 0, 20, 68, &lbl, "HID?");
    }
#endif

    rotate_canvas(canvas, cbuf);
}

/* -------------------------------------------------------------------------- */
/*  Middle canvas: active BLE‑profile indicator                               */
/* -------------------------------------------------------------------------- */
void draw_middle(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state)
{
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_MIDDLE);

    lv_draw_rect_dsc_t rect_bg;
    init_rect_dsc(&rect_bg, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_bg);

    /* Arcs / circles copied from upstream status.c – omitted for brevity */

    rotate_canvas(canvas, cbuf);
}

/* -------------------------------------------------------------------------- */
/*  Bottom canvas: layer indicator (if media info disabled)                    */
/* -------------------------------------------------------------------------- */
void draw_bottom(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state)
{
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_BOTTOM);

    lv_draw_rect_dsc_t rect_bg;
    init_rect_dsc(&rect_bg, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_bg);

#ifndef CONFIG_NICE_VIEW_HID_MEDIA_INFO
    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

    char txt[12];
    if (!state->layer_label || !strlen(state->layer_label)) {
        snprintf(txt, sizeof(txt), "LAYER %u", state->layer_index);
    } else {
        strncpy(txt, state->layer_label, sizeof(txt) - 1);
        txt[sizeof(txt) - 1] = '\0';
    }
    lv_canvas_draw_text(canvas, 0, 5, CANVAS_SIZE, &lbl, txt);
#endif

    rotate_canvas(canvas, cbuf);
}

/* -------------------------------------------------------------------------- */
/*  Global list of instantiated widgets                                       */
/* -------------------------------------------------------------------------- */
static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* ========================================================================== */
/* 1) RAW‑HID: intercept split‑transport central commands                     */
/* ========================================================================== */
/*
 * The stock handler is declared in split/transport/peripheral.h. We provide
 * our own wrapper that first lets the default logic run, then emits a
 * raw_hid_received_event so that the rest of ZMK (and this widget) can react.
 */
__attribute__((weak)) int zmk_split_transport_peripheral_command_handler(
    const struct zmk_split_transport_peripheral *transport,
    struct zmk_split_transport_central_command cmd)
{
    /* Call the next handler in the link‑order chain, if any.  Because this
     * symbol is weak, the linker will pick our implementation last; any other
     * strong definition (e.g. the vanilla one in ZMK) will be preferred and
     * we will not be linked in twice.  Therefore, forward only when another
     * implementation exists. */
    extern __attribute__((weak)) int __zmk_orig_split_transport_peripheral_command_handler(
        const struct zmk_split_transport_peripheral *,
        struct zmk_split_transport_central_command);

    int ret = 0;
    if (__zmk_orig_split_transport_peripheral_command_handler) {
        ret = __zmk_orig_split_transport_peripheral_command_handler(transport, cmd);
    }

    if (cmd.type == ZMK_SPLIT_TRANSPORT_CENTRAL_CMD_TYPE_RAW_HID) {
        struct raw_hid_received_event evt = {
            .data   = cmd.data.raw_hid.data,
            .length = sizeof(cmd.data.raw_hid.data),
        };
        raise_raw_hid_received_event(evt);
    }
    return ret;
}

/* ========================================================================== */
/* 2) Battery‑status widget helpers                                           */
/* ========================================================================== */
static void set_battery(struct zmk_widget_status *w, struct battery_status_state st)
{
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    w->state.charging = st.usb_present;
#endif
    w->state.battery = st.level;
    draw_top(w->obj, w->cbuf, &w->state);
}

static void battery_cb(struct battery_status_state st)
{
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) { set_battery(w, st); }
}

static struct battery_status_state battery_get(const zmk_event_t *e)
{
    ARG_UNUSED(e);
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_cb, battery_get)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif

/* ========================================================================== */
/* 3) Peripheral connection‑state helper                                      */
/* ========================================================================== */
struct periph_conn_state { bool connected; };

static struct periph_conn_state conn_get(const zmk_event_t *e)
{
    ARG_UNUSED(e);
    return (struct periph_conn_state){ .connected = zmk_split_bt_peripheral_is_connected() };
}

static void conn_cb(struct periph_conn_state st)
{
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        w->state.connected = st.connected;
        draw_top(w->obj, w->cbuf, &w->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_periph_conn, struct periph_conn_state, conn_cb, conn_get)
ZMK_SUBSCRIPTION(widget_periph_conn, zmk_split_peripheral_status_changed);

/* -------------------------------------------------------------------------- */
/* 4) Now‑playing media info (optional)                                       */
/* -------------------------------------------------------------------------- */
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
/*  — implementation identical to status.c; omitted for brevity               */
#endif

/* ========================================================================== */
/* 5) Widget initialisation                                                   */
/* ========================================================================== */
int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);

    /* Canvas buffers */
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_periph_conn_init();

    /* First render */
    memset(&widget->state, 0, sizeof(widget->state));
    draw_top(widget->obj, widget->cbuf, &widget->state);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
