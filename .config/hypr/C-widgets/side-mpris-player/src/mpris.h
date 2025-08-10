// src/mpris.h

#ifndef MPRIS_H
#define MPRIS_H

#include <gtk/gtk.h>
#include "lyrics.h"
#include "utils.h"

typedef struct _MprisPopoutState {
    // This is now set by the caller (main.c) after creation.
    GtkWindow *window;
    GDBusProxy *player_proxy;
    gchar *bus_name;

    GtkImage *album_art_image;
    GtkLabel *title_label;
    GtkLabel *artist_label;
    GtkButton *play_pause_button;
    GtkButton *save_lyrics_button;
    
    GtkWidget *toast_label;

    LyricsView *lyrics_view_state;
    GCancellable *lyrics_cancellable;
    gint64 current_lyrics_id;
    gchar *current_track_signature;

    guint next_lyric_timer_id;
    guint resync_poll_timer_id;

    gulong properties_changed_id;
    gulong seeked_id;

    gint64 current_sync_offset_ms;
    GtkLabel *offset_label;
} MprisPopoutState;

// This function now creates a GtkBox widget, not a GtkWindow.
// It returns the state via an output parameter.
GtkWidget* create_mpris_view(const gchar *bus_name, MprisPopoutState **state_out);

#endif // MPRIS_H