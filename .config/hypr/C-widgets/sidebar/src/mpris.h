// src/mpris.h

#ifndef MPRIS_H
#define MPRIS_H

#include <gtk/gtk.h>
#include <adwaita.h>
#include "lyrics.h"
#include "utils.h"

typedef struct _MprisPopoutState {
    GtkWindow *window;
    GDBusProxy *player_proxy;
    gchar *bus_name;

    GtkImage *album_art_image;
    GtkLabel *title_label;
    GtkLabel *artist_label;
    GtkButton *play_pause_button;
    GtkButton *save_lyrics_button;
    AdwToastOverlay *toast_overlay;

    LyricsView *lyrics_view_state;
    GCancellable *lyrics_cancellable;
    gint64 current_lyrics_id;
    gchar *current_track_signature;

    // The ID for the smart, one-shot timer that schedules the next lyric update.
    guint next_lyric_timer_id;
    // The ID for the 10-second safety-net resync timer.
    guint resync_poll_timer_id;

    gulong properties_changed_id;
    gulong seeked_id;
} MprisPopoutState;

GtkWidget* create_mpris_popout(GtkApplication *app, GtkWidget *parent_window, const gchar *bus_name);

#endif // MPRIS_H