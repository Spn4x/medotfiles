#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <glib.h>

// Callback for async operations
typedef void (*AudioOperationCallback)(gboolean success, gpointer user_data);

// Represents one audio output device (a sink)
typedef struct {
    guint id;
    gchar *name;
    gboolean is_default; // Very useful to know which one is active
} AudioSink;

// Represents the complete state of the default audio device
typedef struct {
    gint volume; // 0-100
    gboolean is_muted;
} AudioSinkState;

// Gets the current list of available audio sinks.
// The caller is responsible for freeing the list with free_audio_sink_list().
GList* get_audio_sinks();

// Gets the current volume and mute state of the default sink.
// The caller is responsible for freeing the returned struct.
AudioSinkState* get_default_sink_state();

// Asynchronously sets the volume of the default sink.
void set_default_sink_volume_async(gint volume, AudioOperationCallback callback, gpointer user_data);

// Asynchronously sets the default sink by its ID.
void set_default_sink_async(guint sink_id, AudioOperationCallback callback, gpointer user_data);

// Utility functions to free the memory of our structs
void audio_sink_free(gpointer data);
void free_audio_sink_list(GList *list);

#endif // AUDIO_MANAGER_H