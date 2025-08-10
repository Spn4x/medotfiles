#include "utils.h"
#include <stdio.h>

gchar* run_command(const char *command_line) {
    gchar *stdout_buf = NULL;
    gint exit_status;
    GError *error = NULL;

    // g_spawn_command_line_sync is a robust way to run external commands.
    // It handles quoting and waits for the command to complete.
    g_spawn_command_line_sync(command_line, &stdout_buf, NULL, &exit_status, &error);

    if (error) {
        g_warning("Failed to run command '%s': %s", command_line, error->message);
        g_error_free(error);
        g_free(stdout_buf); // stdout_buf might be partially filled
        return NULL;
    }

    // You might want to check exit_status as well
    // if (exit_status != 0) { ... }
    
    // The buffer is null-terminated by GLib.
    return stdout_buf;
}