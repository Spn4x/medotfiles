// src/lyrics.h

#ifndef LYRICS_H
#define LYRICS_H

#include <gtk/gtk.h>
#include <gio/gio.h>

// Forward declare the MprisPopoutState struct.
// This tells the compiler that the type exists without needing its full definition,
// breaking the circular include chain.
typedef struct _MprisPopoutState MprisPopoutState;

typedef struct {
    guint64 timestamp_ms;
    gchar *line_text;
    GtkWidget *label;
} LyricLine;

typedef struct {
    GtkWidget *scrolled_window;
    GtkWidget *lyrics_box;
    GList *lyric_lines;
    gint current_line_index;
} LyricsView;

GtkWidget* create_lyrics_view(LyricsView **view_out);
void destroy_lyrics_view(LyricsView *view);
void clear_lyrics_display(LyricsView *view);
void parse_and_populate_lyrics(LyricsView *view, const gchar *lrc_data);

// The function signature remains the same, using the now-known forward-declared type.
void lyrics_view_sync_to_position(MprisPopoutState* state, gint64 position_us);

#endif // LYRICS_H