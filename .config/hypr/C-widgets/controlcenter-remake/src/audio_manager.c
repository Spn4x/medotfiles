#include "audio_manager.h"
#include "utils.h" // For run_command
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>

// --- Data Structures for Async Tasks ---
typedef struct {
    guint id_or_volume;
} AudioTaskData;

typedef struct {
    AudioOperationCallback user_callback;
    gpointer user_data;
} AudioFinishData;

// --- Freeing Functions ---
void audio_sink_free(gpointer data) {
    if (!data) return;
    AudioSink *sink = (AudioSink*)data;
    g_free(sink->name);
    g_free(sink);
}
void free_audio_sink_list(GList *list) {
    g_list_free_full(list, audio_sink_free);
}

// --- Syncrhonous "Get" Functions ---

AudioSinkState* get_default_sink_state() {
    g_autofree gchar *output = run_command("wpctl get-volume @DEFAULT_SINK@");
    if (!output) return NULL;

    AudioSinkState *state = g_new0(AudioSinkState, 1);
    state->volume = 0;
    state->is_muted = FALSE;

    if (strstr(output, "[MUTED]")) {
        state->is_muted = TRUE;
    }

    const char *vol_str = strstr(output, "Volume: ");
    if (vol_str) {
        double vol_float = atof(vol_str + 8);
        state->volume = (gint)(vol_float * 100);
    }
    return state;
}

GList* get_audio_sinks() {
    g_autofree gchar *output = run_command("wpctl status");
    if (!output) return NULL;

    GList *sinks = NULL;
    gboolean in_sinks_section = FALSE;
    gchar **lines = g_strsplit(output, "\n", -1);

    for (int i = 0; lines[i] != NULL; i++) {
        const char *line = lines[i];

        if (strstr(line, "Sinks:")) {
            in_sinks_section = TRUE;
            continue;
        }
        if (in_sinks_section && (strstr(line, "Sink endpoints:") || strstr(line, "Sources:"))) {
            break; // End of sinks section
        }
        if (!in_sinks_section || strstr(line, "Easy Effects Sink")) {
            continue;
        }

        const char *dot = strchr(line, '.');
        if (!dot) continue;

        const char *id_start = strpbrk(line, "0123456789");
        if (!id_start) continue;

        AudioSink *sink = g_new0(AudioSink, 1);
        sink->is_default = (strstr(line, "*") != NULL);

        g_autofree gchar *id_str = g_strndup(id_start, dot - id_start);
        sink->id = (guint)atoi(id_str);

        const char *name_start = dot + 2; // Skip ". "
        const char *name_end = strstr(name_start, "[vol:");
        gchar *temp_name = name_end ? g_strndup(name_start, name_end - name_start) : g_strdup(name_start);
        sink->name = g_strstrip(temp_name);
        
        sinks = g_list_append(sinks, sink);
    }

    g_strfreev(lines);
    return sinks;
}

// --- Asynchronous "Set" Functions (using GTask) ---

static void on_async_operation_finished(GObject *s, GAsyncResult *res, gpointer d) {
    AudioFinishData *finish_data = d;
    gboolean success = g_task_propagate_boolean(G_TASK(res), NULL);
    if (finish_data->user_callback) {
        finish_data->user_callback(success, finish_data->user_data);
    }
    g_free(finish_data);
}

static void set_volume_thread_func(GTask *task, gpointer s, gpointer d, GCancellable *c) {
    AudioTaskData *data = g_task_get_task_data(task);
    gchar *cmd = g_strdup_printf("wpctl set-volume @DEFAULT_SINK@ %u%%", data->id_or_volume);
    gint status;
    g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL);
    g_free(cmd);
    g_task_return_boolean(task, status == 0);
    g_free(data);
}

void set_default_sink_volume_async(gint volume, AudioOperationCallback cb, gpointer ud) {
    AudioTaskData *task_data = g_new0(AudioTaskData, 1);
    task_data->id_or_volume = (guint)volume;
    AudioFinishData *finish_data = g_new0(AudioFinishData, 1);
    finish_data->user_callback = cb;
    finish_data->user_data = ud;
    GTask *task = g_task_new(NULL, NULL, on_async_operation_finished, finish_data);
    g_task_set_task_data(task, task_data, NULL);
    g_task_run_in_thread(task, set_volume_thread_func);
    g_object_unref(task);
}

static void set_sink_thread_func(GTask *task, gpointer s, gpointer d, GCancellable *c) {
    AudioTaskData *data = g_task_get_task_data(task);
    gchar *cmd = g_strdup_printf("wpctl set-default %u", data->id_or_volume);
    gint status;
    g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL);
    g_free(cmd);
    g_task_return_boolean(task, status == 0);
    g_free(data);
}

void set_default_sink_async(guint sink_id, AudioOperationCallback cb, gpointer ud) {
    AudioTaskData *task_data = g_new0(AudioTaskData, 1);
    task_data->id_or_volume = sink_id;
    AudioFinishData *finish_data = g_new0(AudioFinishData, 1);
    finish_data->user_callback = cb;
    finish_data->user_data = ud;
    GTask *task = g_task_new(NULL, NULL, on_async_operation_finished, finish_data);
    g_task_set_task_data(task, task_data, NULL);
    g_task_run_in_thread(task, set_sink_thread_func);
    g_object_unref(task);
}