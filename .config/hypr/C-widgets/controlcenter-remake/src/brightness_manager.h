#ifndef BRIGHTNESS_MANAGER_H
#define BRIGHTNESS_MANAGER_H

#include <glib.h>

// Gets the current screen brightness as a percentage (0-100).
// Returns -1 on error.
gint get_current_brightness();

// Asynchronously sets the screen brightness.
void set_brightness_async(gint percentage);

#endif // BRIGHTNESS_MANAGER_H