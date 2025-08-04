// src/mpris.c

#include "mpris.h"
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include <math.h>

// How often the safety-net timer will run to correct any drift.
#define LYRICS_SAFETY_RESYNC_INTERVAL_S 5

// =========================================================================
// == THE FIX FOR LATENCY TUNING ==
// =========================================================================
// This is the master knob for synchronization. It is in milliseconds.
// It compensates for audio buffering latency.
//
// - If lyrics appear LATE (after you hear the words), DECREASE this value.
//   (e.g., from 250 to 0, or from 0 to -500)
// - If lyrics appear EARLY (before you hear the words), INCREASE this value.
//   (e.g., from 150 to 300)
//
// Your reported ~1sec delay suggests we need to significantly advance the clock.
// We will start with a negative offset.
#define LYRICS_SYNC_OFFSET_MS -750
// =========================================================================


// --- Forward Declarations ---
static void on_popout_destroy(GtkWidget *widget, gpointer user_data);
static void fetch_lyrics(MprisPopoutState *state);
static void on_mpris_signal(GDBusProxy *proxy, const gchar *sender, const gchar *signal, GVariant *params, gpointer user_data);
static void launch_lyrics_search_request(MprisPopoutState *state);
static void update_display_metadata(MprisPopoutState *state, GVariantDict *dict);
static void update_playback_status_ui(MprisPopoutState *state, const gchar *status);
static void on_properties_changed(GDBusProxy *proxy, GVariant *changed_properties, const gchar *const *invalidated_properties, gpointer user_data);
static void on_prev_clicked(GtkButton *button, gpointer user_data);
static void on_play_pause_clicked(GtkButton *button, gpointer user_data);
static void on_next_clicked(GtkButton *button, gpointer user_data);
static void on_save_lyrics_clicked(GtkButton *button, gpointer user_data);
static void on_lyrics_api_response(GObject *source, GAsyncResult *res, gpointer user_data);
static gchar* create_track_signature(GVariantDict *dict);
static void save_lyrics_id_for_track(const gchar* signature, gint64 id);
static void update_mpris_state(MprisPopoutState *state);
static void stop_lyric_updates(MprisPopoutState *state);
static void schedule_next_lyric_update(gpointer user_data);
static void on_player_position_for_resync(GObject *source, GAsyncResult *res, gpointer user_data);


// --- Helper Functions ---
static void free_popout_state(gpointer data) {
    MprisPopoutState *state = data;
    if (!state) return;
    g_print("[LIFECYCLE] Destroying MPRIS popout state for %s\n", state->bus_name);
    stop_lyric_updates(state);
    if (state->lyrics_cancellable) g_cancellable_cancel(state->lyrics_cancellable);
    g_clear_object(&state->lyrics_cancellable);
    g_free(state->current_track_signature);
    destroy_lyrics_view(state->lyrics_view_state);
    if (state->player_proxy) {
        if (state->properties_changed_id > 0) g_signal_handler_disconnect(state->player_proxy, state->properties_changed_id);
        if (state->seeked_id > 0) g_signal_handler_disconnect(state->player_proxy, state->seeked_id);
        g_clear_object(&state->player_proxy);
    }
    g_free(state->bus_name);
    g_free(state);
}

static void on_popout_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget; (void)user_data;
    g_print("[LIFECYCLE] MPRIS popout window destroyed signal received.\n");
}


// --- NEW "SMART" LYRICS SYNCHRONIZATION LOGIC ---

// This is the core of the new system. It's called when we need to resynchronize,
// and it schedules the *next* update with a precise one-shot timer.
static void on_player_position_for_resync(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)source;
    MprisPopoutState *state = user_data;
    const char *stdout_str = get_command_stdout(res);

    if (!stdout_str || !state->lyrics_view_state || !state->lyrics_view_state->lyric_lines) {
        return;
    }
    
    // Calculate the adjusted position once.
    gint64 pos_us = (gint64)(atof(stdout_str) * 1000000.0);
    gint64 adjusted_pos_us = pos_us - (LYRICS_SYNC_OFFSET_MS * 1000);
    if (adjusted_pos_us < 0) adjusted_pos_us = 0;

    // STEP 1: Immediately sync the UI to the just-received adjusted position.
    lyrics_view_sync_to_position(state, adjusted_pos_us);

    // STEP 2: Check if playback is active. If not, stop here.
    g_autoptr(GVariant) status_var = g_dbus_proxy_get_cached_property(state->player_proxy, "PlaybackStatus");
    const char *status = status_var ? g_variant_get_string(status_var, NULL) : "Paused";
    if (g_strcmp0(status, "Playing") != 0) {
        return; // Don't schedule the next update if paused.
    }

    // STEP 3: Find the timestamp of the *next* lyric line.
    LyricsView *view = state->lyrics_view_state;
    if (view->current_line_index < 0 || (guint)view->current_line_index + 1 >= g_list_length(view->lyric_lines)) {
        return; // No "next" line, so nothing to schedule.
    }
    GList *next_link = g_list_nth(view->lyric_lines, view->current_line_index + 1);
    LyricLine *next_line = next_link->data;
    guint64 next_timestamp_ms = next_line->timestamp_ms;

    // STEP 4: Calculate the delay until that next line should appear.
    guint64 adjusted_pos_ms = adjusted_pos_us / 1000;
    gint64 delay_ms = next_timestamp_ms - adjusted_pos_ms;

    // If we're already late, schedule an almost-immediate update to catch up.
    if (delay_ms < 0) {
        delay_ms = 20;
    }

    // STEP 5: Schedule the one-shot timer for the next update.
    state->next_lyric_timer_id = g_timeout_add_once((guint)delay_ms, (GSourceOnceFunc)schedule_next_lyric_update, state);
}


// This is the callback for the one-shot timer. It just kicks off a new position check.
static void schedule_next_lyric_update(gpointer user_data) {
    MprisPopoutState *state = user_data;
    if (!state) return;

    // Clear the timer ID since it has already fired.
    state->next_lyric_timer_id = 0;

    // Asynchronously get the player's current position to start the cycle again.
    execute_command_async("playerctl position", on_player_position_for_resync, state);
}

// This is the callback for the 10-second safety net timer.
static gboolean safety_resync_callback(gpointer user_data) {
    MprisPopoutState *state = user_data;
    g_print("[SYNC] Performing 10-second safety resync.\n");

    // Stop any existing smart timer, as this safety check will restart the cycle.
    if (state->next_lyric_timer_id > 0) {
        g_source_remove(state->next_lyric_timer_id);
        state->next_lyric_timer_id = 0;
    }
    
    // Kick off a manual update cycle.
    schedule_next_lyric_update(state);
    return G_SOURCE_CONTINUE; // Keep the safety timer running.
}


// Stops all lyric-related timers.
static void stop_lyric_updates(MprisPopoutState *state) {
    if (state->next_lyric_timer_id > 0) {
        g_source_remove(state->next_lyric_timer_id);
        state->next_lyric_timer_id = 0;
    }
    if (state->resync_poll_timer_id > 0) {
        g_source_remove(state->resync_poll_timer_id);
        state->resync_poll_timer_id = 0;
    }
}


// Starts the lyric synchronization process.
static void start_lyric_updates(MprisPopoutState *state) {
    stop_lyric_updates(state); // Always clear existing timers first.

    g_autoptr(GVariant) status_var = g_dbus_proxy_get_cached_property(state->player_proxy, "PlaybackStatus");
    const char *status = status_var ? g_variant_get_string(status_var, NULL) : "Paused";

    if (g_strcmp0(status, "Playing") == 0) {
        g_print("[SYNC] Playback is active. Starting smart lyric updates.\n");
        // Kick off the smart timer loop immediately.
        schedule_next_lyric_update(state);
        // Also start the 10-second safety-net timer.
        state->resync_poll_timer_id = g_timeout_add_seconds(LYRICS_SAFETY_RESYNC_INTERVAL_S, safety_resync_callback, state);
    } else {
        g_print("[SYNC] Playback is not active. Doing one final sync.\n");
        // If paused/stopped, still do one last check to get the position right.
        execute_command_async("playerctl position", on_player_position_for_resync, state);
    }
}

// --- LYRICS & METADATA ---
static gchar* get_saved_lyrics_path(void) {
    return g_build_filename(g_get_user_cache_dir(), "hypr-sidebar", "saved_lyrics.json", NULL);
}

static void save_lyrics_id_for_track(const gchar* signature, gint64 id) {
    if (!signature) return;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = get_saved_lyrics_path();
    g_autoptr(JsonParser) parser = json_parser_new();
    JsonObject *root_obj = NULL;
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        if (json_parser_load_from_file(parser, path, &error)) {
            root_obj = json_node_get_object(json_parser_get_root(parser));
            if(root_obj) json_object_ref(root_obj);
        } else { g_clear_error(&error); }
    }
    if (!root_obj) root_obj = json_object_new();
    if (id > 0) {
        json_object_set_int_member(root_obj, signature, id);
    } else {
        if (json_object_has_member(root_obj, signature)) json_object_remove_member(root_obj, signature);
    }
    g_autoptr(JsonGenerator) generator = json_generator_new();
    JsonNode *root_node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(root_node, root_obj);
    json_generator_set_root(generator, root_node);
    json_generator_set_pretty(generator, TRUE);
    g_autofree gchar *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    json_generator_to_file(generator, path, NULL);
}

static gint64 load_lyrics_id_for_track(const gchar* signature) {
    if (!signature) return 0;
    g_autofree gchar *path = get_saved_lyrics_path();
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) return 0;
    g_autoptr(GError) error = NULL;
    g_autoptr(JsonParser) parser = json_parser_new();
    if (!json_parser_load_from_file(parser, path, &error)) return 0;
    JsonObject *root_obj = json_node_get_object(json_parser_get_root(parser));
    if (json_object_has_member(root_obj, signature)) {
        return json_object_get_int_member(root_obj, signature);
    }
    return 0;
}

static gchar* create_track_signature(GVariantDict *dict) {
    const char *title = NULL, *artist = NULL, *album = NULL;
    gchar **artists = NULL;
    if (!dict) return NULL;
    g_variant_dict_lookup(dict, "xesam:title", "&s", &title);
    g_variant_dict_lookup(dict, "xesam:artist", "^as", &artists);
    g_variant_dict_lookup(dict, "xesam:album", "&s", &album);
    if (artists && artists[0]) artist = artists[0];
    gchar *signature = (title && artist) ? g_strdup_printf("%s - %s", artist, title) : NULL;
    g_strfreev(artists);
    return signature;
}

static void process_lyrics_response_node(JsonNode *root_node, MprisPopoutState *state) {
    const char *best_match_lyrics = NULL;
    gint64 best_match_id = 0;

    if (JSON_NODE_HOLDS_OBJECT(root_node)) {
        JsonObject *root_obj = json_node_get_object(root_node);
        if (json_object_has_member(root_obj, "code") && json_object_get_int_member(root_obj, "code") == 404) {
            if (state->current_track_signature) save_lyrics_id_for_track(state->current_track_signature, 0);
            launch_lyrics_search_request(state);
            return;
        }
        best_match_lyrics = json_object_get_string_member_with_default(root_obj, "syncedLyrics", NULL);
        best_match_id = json_object_get_int_member_with_default(root_obj, "id", 0);
    } else if (JSON_NODE_HOLDS_ARRAY(root_node)) {
        JsonArray *results_array = json_node_get_array(root_node);
        double min_duration_diff = 1000.0;
        gint64 original_duration_us = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(state->window), "current-track-duration"));
        guint original_duration_s = (guint)(original_duration_us / 1000000);
        for (guint i = 0; i < json_array_get_length(results_array); i++) {
            JsonObject *track_obj = json_array_get_object_element(results_array, i);
            const char *synced_lyrics = json_object_get_string_member_with_default(track_obj, "syncedLyrics", NULL);
            if (synced_lyrics && *synced_lyrics) {
                if (original_duration_us <= 0) {
                    best_match_lyrics = synced_lyrics;
                    best_match_id = json_object_get_int_member_with_default(track_obj, "id", 0);
                    break;
                }
                gint result_duration = json_object_get_int_member_with_default(track_obj, "duration", -1);
                double diff = fabs((double)result_duration - (double)original_duration_s);
                if (diff < min_duration_diff) {
                    min_duration_diff = diff;
                    best_match_lyrics = synced_lyrics;
                    best_match_id = json_object_get_int_member_with_default(track_obj, "id", 0);
                }
            }
        }
        if (best_match_lyrics && !(min_duration_diff <= 2.0 || original_duration_us <= 0)) {
            best_match_lyrics = NULL;
            best_match_id = 0;
        }
    }

    if (best_match_lyrics && *best_match_lyrics) {
        g_print("[API] Found lyrics. ID: %"G_GINT64_FORMAT". Populating view.\n", best_match_id);
        state->current_lyrics_id = best_match_id;
        gtk_widget_set_sensitive(GTK_WIDGET(state->save_lyrics_button), TRUE);
        parse_and_populate_lyrics(state->lyrics_view_state, best_match_lyrics);
    } else {
        if (g_object_get_data(G_OBJECT(state->window), "is-fallback-search")) {
            g_print("[API] No suitable match found in search results.\n");
            parse_and_populate_lyrics(state->lyrics_view_state, "");
        } else {
            launch_lyrics_search_request(state);
        }
    }
    // Final step: re-evaluate the main state machine now that lyrics are loaded.
    update_mpris_state(state);
}

static void on_lyrics_api_response(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    MprisPopoutState *state = user_data;
    g_autoptr(GError) error = NULL;
    if (g_cancellable_is_cancelled(state->lyrics_cancellable)) return;

    g_autoptr(GInputStream) stream = soup_session_send_finish(SOUP_SESSION(source_object), res, &error);
    if (error) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) launch_lyrics_search_request(state);
        return;
    }
    g_autoptr(JsonParser) parser = json_parser_new();
    if (!json_parser_load_from_stream(parser, stream, state->lyrics_cancellable, &error)) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) launch_lyrics_search_request(state);
        return;
    }
    process_lyrics_response_node(json_parser_get_root(parser), state);
}

static void launch_lyrics_search_request(MprisPopoutState *state) {
    g_object_set_data(G_OBJECT(state->window), "is-fallback-search", GINT_TO_POINTER(1));
    g_autoptr(GVariant) metadata_var = g_dbus_proxy_get_cached_property(state->player_proxy, "Metadata");
    if (!metadata_var) { parse_and_populate_lyrics(state->lyrics_view_state, ""); return; }
    g_autoptr(GVariantDict) dict = g_variant_dict_new(metadata_var);
    const char *title = NULL; gchar **artists = NULL;
    g_variant_dict_lookup(dict, "xesam:title", "&s", &title);
    g_variant_dict_lookup(dict, "xesam:artist", "^as", &artists);
    if (title && artists && artists[0]) {
        g_autofree gchar *artist_encoded = g_uri_escape_string(artists[0], NULL, FALSE);
        g_autofree gchar *title_encoded = g_uri_escape_string(title, NULL, FALSE);
        g_autofree gchar *search_url = g_strdup_printf("https://lrclib.net/api/search?track_name=%s&artist_name=%s", title_encoded, artist_encoded);
        g_print("[API] Fallback search: %s\n", search_url);
        g_autoptr(SoupSession) session = soup_session_new();
        g_autoptr(SoupMessage) msg = soup_message_new("GET", search_url);
        soup_message_headers_append(soup_message_get_request_headers(msg), "User-Agent", "HyprSidebar(v2.1)");
        soup_session_send_async(session, msg, G_PRIORITY_DEFAULT, state->lyrics_cancellable, (GAsyncReadyCallback)on_lyrics_api_response, state);
    } else {
        parse_and_populate_lyrics(state->lyrics_view_state, "");
    }
    g_strfreev(artists);
}

static void update_playback_status_ui(MprisPopoutState *state, const gchar *status) {
    if (!state || !status) return;
    gtk_button_set_icon_name(state->play_pause_button, g_strcmp0(status, "Playing") == 0 ? "media-playback-pause-symbolic" : "media-playback-start-symbolic");
}

static void fetch_lyrics(MprisPopoutState *state) {
    if (state->lyrics_cancellable) {
        g_cancellable_cancel(state->lyrics_cancellable);
        g_clear_object(&state->lyrics_cancellable);
    }
    state->lyrics_cancellable = g_cancellable_new();
    state->current_lyrics_id = 0;
    gtk_widget_set_sensitive(GTK_WIDGET(state->save_lyrics_button), FALSE);
    g_object_set_data(G_OBJECT(state->window), "is-fallback-search", NULL);
    parse_and_populate_lyrics(state->lyrics_view_state, NULL);

    gint64 saved_id = load_lyrics_id_for_track(state->current_track_signature);
    g_autoptr(SoupSession) session = soup_session_new();
    if (saved_id > 0) {
        g_autofree gchar *url = g_strdup_printf("https://lrclib.net/api/get/%"G_GINT64_FORMAT, saved_id);
        g_print("[API] Fetch with saved ID: %s\n", url);
        g_autoptr(SoupMessage) msg = soup_message_new("GET", url);
        soup_message_headers_append(soup_message_get_request_headers(msg), "User-Agent", "HyprSidebar(v2.1)");
        soup_session_send_async(session, msg, G_PRIORITY_DEFAULT, state->lyrics_cancellable, (GAsyncReadyCallback)on_lyrics_api_response, state);
        return;
    }
    
    g_autoptr(GVariant) metadata_var = g_dbus_proxy_get_cached_property(state->player_proxy, "Metadata");
    if (!metadata_var) { launch_lyrics_search_request(state); return; }
    g_autoptr(GVariantDict) dict = g_variant_dict_new(metadata_var);
    const char *title = NULL, *album = NULL; gchar **artists = NULL; gint64 length_us = 0;
    g_variant_dict_lookup(dict, "xesam:title", "&s", &title);
    g_variant_dict_lookup(dict, "xesam:artist", "^as", &artists);
    g_variant_dict_lookup(dict, "mpris:length", "t", &length_us);
    g_variant_dict_lookup(dict, "xesam:album", "&s", &album);
    g_object_set_data(G_OBJECT(state->window), "current-track-duration", GINT_TO_POINTER(length_us));

    if (title && artists && artists[0] && album && length_us > 0) {
        g_autofree gchar *artist_enc = g_uri_escape_string(artists[0], NULL, FALSE);
        g_autofree gchar *title_enc = g_uri_escape_string(title, NULL, FALSE);
        g_autofree gchar *album_enc = g_uri_escape_string(album, NULL, FALSE);
        guint duration_s = (guint)(length_us / 1000000);
        g_autofree gchar *url = g_strdup_printf("https://lrclib.net/api/get?track_name=%s&artist_name=%s&album_name=%s&duration=%u", title_enc, artist_enc, album_enc, duration_s);
        g_print("[API] Precise fetch: %s\n", url);
        g_autoptr(SoupMessage) msg = soup_message_new("GET", url);
        soup_message_headers_append(soup_message_get_request_headers(msg), "User-Agent", "HyprSidebar(v2.1)");
        soup_session_send_async(session, msg, G_PRIORITY_DEFAULT, state->lyrics_cancellable, (GAsyncReadyCallback)on_lyrics_api_response, state);
    } else {
        launch_lyrics_search_request(state);
    }
    g_strfreev(artists);
}

static void update_display_metadata(MprisPopoutState *state, GVariantDict *dict) {
    if (!state || !dict) return;
    const char *title = NULL, *art_url = NULL; gchar **artists = NULL;
    g_variant_dict_lookup(dict, "xesam:title", "&s", &title);
    g_variant_dict_lookup(dict, "xesam:artist", "^as", &artists);
    g_variant_dict_lookup(dict, "mpris:artUrl", "&s", &art_url);
    gtk_label_set_text(state->title_label, title ? title : "Unknown Title");
    gtk_label_set_text(state->artist_label, (artists && artists[0]) ? artists[0] : "Unknown Artist");
    if (art_url && g_str_has_prefix(art_url, "file://")) {
        g_autofree gchar *path = g_filename_from_uri(art_url, NULL, NULL);
        gtk_image_set_from_file(state->album_art_image, path);
    } else {
        gtk_image_set_from_icon_name(state->album_art_image, "audio-x-generic");
    }
    g_strfreev(artists);
}

// --- DBus Signal Handlers & State Machine ---
static void on_mpris_signal(GDBusProxy *proxy, const gchar *sender, const gchar *name, GVariant *params, gpointer user_data) {
    (void)proxy; (void)sender; (void)params;
    MprisPopoutState *state = user_data;
    if (g_strcmp0(name, "Seeked") == 0) {
        g_print("[SIGNAL] Player Seeked. Triggering immediate resync.\n");
        // Kick off the smart update loop.
        schedule_next_lyric_update(state);
    }
}

static void on_properties_changed(GDBusProxy *proxy, GVariant *changed_properties, const gchar *const *invalidated_properties, gpointer user_data) {
    (void)proxy; (void)invalidated_properties;
    MprisPopoutState *state = user_data;
    g_autoptr(GVariantDict) dict = g_variant_dict_new(changed_properties);

    // We only care about major property changes here. Position is handled by our timers.
    if (g_variant_dict_contains(dict, "Metadata") ||
        g_variant_dict_contains(dict, "PlaybackStatus")) {
        g_print("[SIGNAL] Significant property changed. Triggering state update.\n");
        update_mpris_state(state);
    }
}


// THIS IS THE AUTHORITATIVE STATE MACHINE
static void update_mpris_state(MprisPopoutState *state) {
    if (!state || !state->player_proxy) return;

    // 1. Get FRESH data for metadata and status.
    g_autoptr(GVariant) metadata_var = g_dbus_proxy_get_cached_property(state->player_proxy, "Metadata");
    g_autoptr(GVariantDict) dict = metadata_var ? g_variant_dict_new(metadata_var) : NULL;
    
    g_autoptr(GVariant) status_var = g_dbus_proxy_get_cached_property(state->player_proxy, "PlaybackStatus");
    const char *status = status_var ? g_variant_get_string(status_var, NULL) : "Paused";

    // 2. ALWAYS update the non-lyric UI (art, title, artist, play/pause button).
    // This handles the case where artUrl arrives slightly after the title.
    update_display_metadata(state, dict);
    update_playback_status_ui(state, status);

    // 3. Check if it's a new track to decide if we need to fetch new LYRICS.
    g_autofree gchar *new_signature = create_track_signature(dict);
    if (new_signature && g_strcmp0(new_signature, state->current_track_signature) != 0) {
        g_print("[STATE] New Track Detected: %s\n", new_signature);

        // Update internal track signature
        g_free(state->current_track_signature);
        state->current_track_signature = g_strdup(new_signature);
        
        // Fetch lyrics for the new track
        fetch_lyrics(state);
    }
    
    // 4. ALWAYS re-evaluate the lyric update timers based on the current playback status.
    start_lyric_updates(state);
}

// --- Player Controls & Actions ---
static void on_prev_clicked(GtkButton*b,gpointer d){(void)b;g_dbus_proxy_call(((MprisPopoutState*)d)->player_proxy,"Previous",NULL,G_DBUS_CALL_FLAGS_NONE,-1,NULL,NULL,NULL);}
static void on_play_pause_clicked(GtkButton*b,gpointer d){(void)b;g_dbus_proxy_call(((MprisPopoutState*)d)->player_proxy,"PlayPause",NULL,G_DBUS_CALL_FLAGS_NONE,-1,NULL,NULL,NULL);}
static void on_next_clicked(GtkButton*b,gpointer d){(void)b;g_dbus_proxy_call(((MprisPopoutState*)d)->player_proxy,"Next",NULL,G_DBUS_CALL_FLAGS_NONE,-1,NULL,NULL,NULL);}
static void on_save_lyrics_clicked(GtkButton *b,gpointer d){(void)b;MprisPopoutState*s=d;if(!s||!s->current_track_signature||s->current_lyrics_id<=0)return;save_lyrics_id_for_track(s->current_track_signature,s->current_lyrics_id);adw_toast_overlay_add_toast(s->toast_overlay,adw_toast_new("Lyrics preference saved"));}

// --- Main Public Function ---
GtkWidget* create_mpris_popout(GtkApplication *app, GtkWidget *parent, const gchar *bus_name) {
    g_return_val_if_fail(GTK_IS_APPLICATION(app),NULL);g_return_val_if_fail(GTK_IS_WINDOW(parent),NULL);g_return_val_if_fail(bus_name!=NULL,NULL);
    g_print("[LIFECYCLE] Creating MPRIS popout for %s\n", bus_name);
    GtkWindow *win = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_transient_for(win, GTK_WINDOW(parent));
    gtk_window_set_default_size(win, 300, 450);
    gtk_widget_add_css_class(GTK_WIDGET(win), "mpris-popout");
    MprisPopoutState *state = g_new0(MprisPopoutState, 1);
    state->window = win;
    state->bus_name = g_strdup(bus_name);

    g_object_set_data_full(G_OBJECT(win), "mpris-state", state, free_popout_state);
    g_signal_connect(win, "destroy", G_CALLBACK(on_popout_destroy), NULL);
    state->toast_overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    gtk_window_set_child(win, GTK_WIDGET(state->toast_overlay));
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
    gtk_widget_set_margin_start(main_box,15);gtk_widget_set_margin_end(main_box,15);gtk_widget_set_margin_top(main_box,15);gtk_widget_set_margin_bottom(main_box,15);
    adw_toast_overlay_set_child(state->toast_overlay,main_box);
    GtkWidget *info_box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,15);
    state->album_art_image=GTK_IMAGE(gtk_image_new());
    gtk_image_set_pixel_size(state->album_art_image,84);
    gtk_widget_add_css_class(GTK_WIDGET(state->album_art_image),"album-art");
    GtkWidget *text_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);
    gtk_widget_set_valign(text_box,GTK_ALIGN_CENTER);
    state->title_label=GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(state->title_label),"title-label");
    gtk_label_set_xalign(state->title_label,0.0);
    gtk_label_set_wrap(state->title_label,TRUE);
    state->artist_label=GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(state->artist_label),"artist-label");
    gtk_label_set_xalign(state->artist_label,0.0);
    gtk_box_append(GTK_BOX(text_box),GTK_WIDGET(state->title_label));
    gtk_box_append(GTK_BOX(text_box),GTK_WIDGET(state->artist_label));
    gtk_box_append(GTK_BOX(info_box),GTK_WIDGET(state->album_art_image));
    gtk_box_append(GTK_BOX(info_box),text_box);
    gtk_box_append(GTK_BOX(main_box),info_box);
    GtkWidget *ctrl_box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
    gtk_widget_set_halign(ctrl_box,GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(ctrl_box,"linked");
    GtkWidget *prev=gtk_button_new_from_icon_name("media-skip-backward-symbolic");
    state->play_pause_button=GTK_BUTTON(gtk_button_new_from_icon_name("media-playback-start-symbolic"));
    state->save_lyrics_button=GTK_BUTTON(gtk_button_new_from_icon_name("document-save-symbolic"));
    GtkWidget *next=gtk_button_new_from_icon_name("media-skip-forward-symbolic");
    gtk_widget_set_tooltip_text(GTK_WIDGET(state->save_lyrics_button),"Save this lyric version for future plays");
    gtk_widget_set_sensitive(GTK_WIDGET(state->save_lyrics_button),FALSE);
    g_signal_connect(prev,"clicked",G_CALLBACK(on_prev_clicked),state);
    g_signal_connect(state->play_pause_button,"clicked",G_CALLBACK(on_play_pause_clicked),state);
    g_signal_connect(state->save_lyrics_button,"clicked",G_CALLBACK(on_save_lyrics_clicked),state);
    g_signal_connect(next,"clicked",G_CALLBACK(on_next_clicked),state);
    gtk_box_append(GTK_BOX(ctrl_box),prev);
    gtk_box_append(GTK_BOX(ctrl_box),GTK_WIDGET(state->play_pause_button));
    gtk_box_append(GTK_BOX(ctrl_box),GTK_WIDGET(state->save_lyrics_button));
    gtk_box_append(GTK_BOX(ctrl_box),next);
    gtk_box_append(GTK_BOX(main_box),ctrl_box);
    GtkWidget *lyrics_widget = create_lyrics_view(&state->lyrics_view_state);
    gtk_box_append(GTK_BOX(main_box), lyrics_widget);
    g_autoptr(GError) error = NULL;
    state->player_proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL, bus_name, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", NULL, &error);
    if (error) {
        adw_toast_overlay_add_toast(state->toast_overlay, adw_toast_new("Could not connect to player."));
    } else {
        g_print("[LIFECYCLE] Successfully connected to player. Connecting signals.\n");
        state->properties_changed_id = g_signal_connect(state->player_proxy, "g-properties-changed", G_CALLBACK(on_properties_changed), state);
        state->seeked_id = g_signal_connect(state->player_proxy, "g-signal", G_CALLBACK(on_mpris_signal), state);
        update_mpris_state(state); // Initial state check
    }
    return GTK_WIDGET(win);
}