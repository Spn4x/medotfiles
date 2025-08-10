#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <glib.h>

// Enum to identify the type of event that occurred.
typedef enum {
    SYSTEM_EVENT_VOLUME_CHANGED,
    SYSTEM_EVENT_BRIGHTNESS_CHANGED
} SystemEventType;

// The callback function that the UI will provide.
typedef void (*SystemEventCallback)(SystemEventType type, gpointer user_data);

typedef struct _SystemMonitor SystemMonitor;

// Creates a new monitor. It will immediately start listening for events.
SystemMonitor* system_monitor_new(SystemEventCallback callback, gpointer user_data);

// Stops listening and frees all resources.
void system_monitor_free(SystemMonitor *monitor);

#endif // SYSTEM_MONITOR_H