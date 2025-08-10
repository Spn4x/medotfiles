#include "brightness_manager.h"
#include "utils.h" // For run_command
#include <gio/gio.h>
#include <stdlib.h> // Needed for atoi

// Get current brightness as percentage
gint get_current_brightness() {
    // Use separate 'get' and 'max' calls for robustness instead of the machine-readable format.
    g_autofree gchar *current_str = run_command("brightnessctl get");
    g_autofree gchar *max_str = run_command("brightnessctl max");

    if (!current_str || !max_str) {
        g_warning("Failed to run brightnessctl 'get' or 'max' command.");
        return -1;
    }

    // atoi is sufficient and handles trailing newlines from command output.
    gint current = atoi(current_str);
    gint max = atoi(max_str);

    if (max > 0) {
        // Calculate percentage
        return (gint)(((gdouble)current * 100.0) / (gdouble)max);
    }

    g_warning("Could not calculate brightness percentage: max value from brightnessctl is invalid (<= 0).");
    return -1;
}

// --- Async Set Logic ---

typedef struct {
    gint percentage;
} BrightnessTaskData;

// Run this in thread to avoid blocking UI
static void set_brightness_thread_func(GTask *task, gpointer s, gpointer d, GCancellable *c) {
    (void)s;
    (void)d;
    (void)c;

    BrightnessTaskData *data = g_task_get_task_data(task);
    gchar *cmd = g_strdup_printf("brightnessctl set %d%%", data->percentage);

    // Execute the command
    g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL);

    g_free(cmd);
    g_task_return_boolean(task, TRUE);
    g_free(data);
}

// Public function to set brightness asynchronously
void set_brightness_async(gint percentage) {
    BrightnessTaskData *task_data = g_new0(BrightnessTaskData, 1);
    task_data->percentage = percentage;

    GTask *task = g_task_new(NULL, NULL, NULL, NULL);
    g_task_set_task_data(task, task_data, NULL);
    g_task_run_in_thread(task, set_brightness_thread_func);
    g_object_unref(task);
}