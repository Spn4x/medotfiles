// src/utils.c

#include "utils.h"
#include <stdio.h>

typedef struct {
    GTask *task;
    char *command;
} CommandData;

static void on_command_finished(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    CommandData *data = (CommandData*)user_data;
    GSubprocess *proc = G_SUBPROCESS(source_object);
    g_autoptr(GError) error = NULL;
    gchar *stdout_buffer = NULL;
    gchar *stderr_buffer = NULL;

    g_subprocess_communicate_utf8_finish(proc, res, &stdout_buffer, &stderr_buffer, &error);
    
    if (error) {
        g_warning("COMMAND FAILED: \"%s\"\n--- ERROR ---\n%s\n-------------\n", data->command, error->message);
        g_task_return_error(data->task, error);
    } else {
        // This is the robust pattern. We return ownership of stdout_buffer to the task.
        // The task will call g_free on it if nothing else claims it.
        g_task_return_pointer(data->task, stdout_buffer, g_free);
    }
    
    // We are done with stderr, free it immediately.
    g_free(stderr_buffer);
    
    g_object_unref(data->task);
    g_free(data->command);
    g_free(data);
}

void execute_command_async(const char* command, GAsyncReadyCallback callback, gpointer user_data) {
    g_autoptr(GError) error = NULL;
    gchar **argv = NULL;
    if (!g_shell_parse_argv(command, NULL, &argv, &error)) {
        g_warning("Error parsing command: %s", error->message);
        g_error_free(error);
        return;
    }

    GSubprocess *proc = g_subprocess_newv((const gchar * const *)argv,
                                          G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                          &error);
    g_strfreev(argv);
    if (error) {
        g_warning("Error creating subprocess: %s", error->message);
        g_error_free(error);
        return;
    }
    
    CommandData *data = g_new(CommandData, 1);
    data->task = g_task_new(proc, NULL, callback, user_data);
    data->command = g_strdup(command);
    
    g_subprocess_communicate_utf8_async(proc, NULL, NULL, on_command_finished, data);
}

// This function now correctly propagates ownership of the string.
gchar* get_command_stdout(GAsyncResult *res) {
    g_autoptr(GError) error = NULL;
    // This transfers ownership of the string from the task to us.
    gchar *stdout_str = g_task_propagate_pointer(G_TASK(res), &error);
    if (error) {
        g_warning("Could not get command stdout: %s", error->message);
        g_clear_pointer(&stdout_str, g_free); // Free the string if an error occurred.
        return NULL;
    }
    return stdout_str; // The caller now owns the string.
}