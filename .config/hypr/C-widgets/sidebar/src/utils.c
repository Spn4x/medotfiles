#include "utils.h"
#include <stdio.h>

// ++ MODIFIED: The result struct now holds both stdout and stderr ++
typedef struct {
    gchar *stdout_buffer;
    gchar *stderr_buffer;
} CommandResult;

typedef struct {
    GTask *task;
    char *command;
} CommandData;

static void on_command_finished(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    CommandData *data = (CommandData*)user_data;
    GSubprocess *proc = G_SUBPROCESS(source_object);
    CommandResult *result = g_new0(CommandResult, 1);
    GError *error = NULL;

    g_subprocess_communicate_utf8_finish(proc, res, &result->stdout_buffer, &result->stderr_buffer, &error);
    
    if (error) {
        g_warning("COMMAND FAILED: \"%s\"\n--- ERROR ---\n%s\n-------------\n", data->command, error->message);
        g_task_return_error(data->task, error);
        g_free(result->stdout_buffer);
        g_free(result->stderr_buffer);
        g_free(result);
    } else {
        // Log both stdout and stderr for debugging
        g_print("COMMAND SUCCEEDED: \"%s\"\n--- STDOUT ---\n%s\n--- STDERR ---\n%s\n--------------\n", 
                data->command, 
                result->stdout_buffer ? result->stdout_buffer : "[No output]",
                result->stderr_buffer ? result->stderr_buffer : "[No error output]");
        g_task_return_pointer(data->task, result, (GDestroyNotify)g_free);
    }
    
    g_object_unref(data->task);
    g_free(data->command);
    g_free(data);
}

void execute_command_async(const char* command, GAsyncReadyCallback callback, gpointer user_data) {
    GError *error = NULL;
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
    
    g_print("EXECUTING COMMAND: \"%s\"\n", command);
    
    CommandData *data = g_new(CommandData, 1);
    data->task = g_task_new(proc, NULL, callback, user_data);
    data->command = g_strdup(command);
    
    g_subprocess_communicate_utf8_async(proc, NULL, NULL, on_command_finished, data);
}

// ++ MODIFIED: These helpers now return the specific streams ++
const char* get_command_stdout(GAsyncResult *res) {
    GError *error = NULL;
    CommandResult *result = g_task_propagate_pointer(G_TASK(res), &error);
    if (error || !result) {
        if (error) g_error_free(error);
        return NULL;
    }
    return result->stdout_buffer;
}

const char* get_command_stderr(GAsyncResult *res) {
    GError *error = NULL;
    CommandResult *result = g_task_propagate_pointer(G_TASK(res), &error);
    if (error || !result) {
        if (error) g_error_free(error);
        return NULL;
    }
    return result->stderr_buffer;
}