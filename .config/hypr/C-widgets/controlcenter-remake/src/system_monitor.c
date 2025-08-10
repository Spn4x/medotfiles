// ===== src/system_monitor.c =====
#include "system_monitor.h"
#include "utils.h"
#include <gio/gio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>

struct _SystemMonitor {
    SystemEventCallback callback;
    gpointer user_data;

    // Volume
    GPid       pactl_pid;
    GIOChannel *pactl_channel;
    guint      pactl_watch_id;

    // Brightness
    GFileMonitor *brightness_monitor;
    gchar        *brightness_device;
};

// --- FORWARD DECLARATION ---
// This tells the compiler that this function exists before it is used.
static void on_brightness_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data);

// --- pactl (Volume) Monitor Logic ---
static gboolean on_pactl_output(GIOChannel *source, GIOCondition condition, gpointer user_data) {
    (void)condition;
    SystemMonitor *sm = user_data;
    g_autofree gchar *line = NULL;
    gsize len;

    if (g_io_channel_read_line(source, &line, &len, NULL, NULL) == G_IO_STATUS_NORMAL) {
        if (strstr(line, "on sink #") || strstr(line, "on server")) {
            sm->callback(SYSTEM_EVENT_VOLUME_CHANGED, sm->user_data);
        }
    }
    return G_SOURCE_CONTINUE;
}

static void start_volume_monitor(SystemMonitor *sm) {
    const gchar *pactl_path = "/usr/bin/pactl";
    if (access(pactl_path, X_OK) != 0) {
        g_warning("'pactl' not found at %s", pactl_path);
        return;
    }
    gchar *argv[] = { (gchar *)pactl_path, "subscribe", NULL };
    gint stdout_fd;
    GError *error = NULL;

    if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &sm->pactl_pid, NULL, &stdout_fd, NULL, &error)) {
        g_warning("Failed to spawn pactl: %s", error->message);
        g_error_free(error);
        return;
    }
    g_print("Started 'pactl subscribe' PID %d\n", sm->pactl_pid);
    sm->pactl_channel = g_io_channel_unix_new(stdout_fd);
    g_io_channel_set_encoding(sm->pactl_channel, NULL, NULL);
    g_io_channel_set_flags(sm->pactl_channel, G_IO_FLAG_NONBLOCK, NULL);
    sm->pactl_watch_id = g_io_add_watch(sm->pactl_channel, G_IO_IN | G_IO_HUP, on_pactl_output, sm);
}

// --- Asynchronous Brightness Monitor Logic ---

// This function is run in a background thread to avoid blocking the UI.
static void find_brightness_device_thread_func(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable) {
    (void)task; (void)source_object; (void)task_data; (void)cancellable;
    const char *cmd = "sh -c \"brightnessctl -l -m | head -n1 | cut -d, -f1\"";
    g_autofree gchar *device = run_command(cmd);

    if (device) {
        g_strstrip(device);
        gboolean all_digits = TRUE;
        for (gchar *p = device; *p; ++p) {
            if (!g_ascii_isdigit(*p)) { all_digits = FALSE; break; }
        }
        if (all_digits || strlen(device) == 0) {
            g_free(device);
            device = NULL; // Will trigger fallback in the finish function
        }
    }
    // Return the found device name (or NULL) as the result of the task.
    g_task_return_pointer(task, g_strdup(device), g_free);
}

// Helper to find a fallback device if brightnessctl fails.
static gchar* fallback_brightness_device(void) {
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return NULL;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        closedir(d);
        return g_strdup(ent->d_name);
    }
    closedir(d);
    return NULL;
}

// This function is called on the main thread when the background task is finished.
static void on_brightness_device_found(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    SystemMonitor *sm = user_data;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *device_name = g_task_propagate_pointer(G_TASK(res), &error);

    if (error) {
        g_warning("Could not find brightness device: %s", error->message);
        return;
    }
    
    // If the command failed, try a fallback method.
    if (device_name == NULL) {
        device_name = fallback_brightness_device();
    }
    
    if (device_name == NULL) {
        g_warning("Could not determine brightness device name at all.");
        return;
    }

    sm->brightness_device = g_strdup(device_name);
    g_autofree gchar *path = g_strdup_printf("/sys/class/backlight/%s/actual_brightness", sm->brightness_device);
    g_print("Monitoring brightness at: %s\n", path);

    GFile *file = g_file_new_for_path(path);
    // Note: GFileMonitor must be created and used on the same thread, which is why we do it here.
    sm->brightness_monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, NULL);
    g_object_unref(file);

    if (!sm->brightness_monitor) {
        g_warning("Failed to create file monitor for %s", path);
        return;
    }
    g_signal_connect(sm->brightness_monitor, "changed", G_CALLBACK(on_brightness_file_changed), sm);
}

// Brightness-change handler (the actual event callback)
static void on_brightness_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {
    (void)monitor; (void)file; (void)other_file;
    if (event_type == G_FILE_MONITOR_EVENT_CHANGED) {
        SystemMonitor *sm = user_data;
        sm->callback(SYSTEM_EVENT_BRIGHTNESS_CHANGED, sm->user_data);
    }
}

// This function now starts the *asynchronous* process. It returns instantly.
static void start_brightness_monitor_async(SystemMonitor *sm) {
    GTask *task = g_task_new(NULL, NULL, on_brightness_device_found, sm);
    g_task_run_in_thread(task, find_brightness_device_thread_func);
    g_object_unref(task); // The task will hold a ref to itself until it's done.
}

// --- Public API ---
SystemMonitor* system_monitor_new(SystemEventCallback callback, gpointer user_data) {
    SystemMonitor *sm = g_new0(SystemMonitor, 1);
    sm->callback = callback;
    sm->user_data = user_data;

    start_volume_monitor(sm);
    start_brightness_monitor_async(sm); // Use the new non-blocking function
    
    return sm;
}

void system_monitor_free(SystemMonitor *sm) {
    if (!sm) return;

    if (sm->pactl_watch_id > 0)
        g_source_remove(sm->pactl_watch_id);
    if (sm->pactl_channel)
        g_io_channel_unref(sm->pactl_channel);
    if (sm->pactl_pid > 0) {
        kill(sm->pactl_pid, SIGTERM);
        g_spawn_close_pid(sm->pactl_pid);
    }
    if (sm->brightness_monitor) {
        g_file_monitor_cancel(sm->brightness_monitor);
        g_object_unref(sm->brightness_monitor);
    }
    g_free(sm->brightness_device);
    g_free(sm);
}