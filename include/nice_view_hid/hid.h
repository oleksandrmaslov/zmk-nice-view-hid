#pragma once

#include <zmk/event_manager.h>

struct time_notification {
    uint8_t hour;
    uint8_t minute;
};

ZMK_EVENT_DECLARE(time_notification);

struct volume_notification {
    uint8_t value;
};

ZMK_EVENT_DECLARE(volume_notification);

struct layout_notification {
    uint8_t value;
};

ZMK_EVENT_DECLARE(layout_notification);