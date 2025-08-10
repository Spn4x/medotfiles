// src/main.c

#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include "mpris.h"

static const int PLAYER_VERTICAL_OFFSET = 100;
static const int HORIZONTAL_OFFSET = -1;

typedef struct {
    GtkApplication *app;
    GtkWidget *main_window;

    // State for the currently displayed view
    GtkWidget *current_view;
    MprisPopoutState *mpris_state;

    // List of active MPRIS players (bus names)
    GList *mpris_players;

    // DBus connection and watcher ID
    GDBusConnection *dbus_connection;
    guint name_watcher_id;

    // CSS
    GtkCssProvider *css_provider;
    GFileMonitor *css_file_monitor;
} AppState;

// --- Forward Declarations ---
static void update_view(AppState *state);
static GtkWidget* create_default_view();

static void on_mpris_name_appeared(const gchar *name, gpointer user_data) {
    AppState *state = user_data;
    if (g_list_find_custom(state->mpris_players, name, (GCompareFunc)g_strcmp0) == NULL) {
        state->mpris_players = g_list_append(state->mpris_players, g_strdup(name));
        update_view(state);
    }
}

static void on_mpris_name_vanished(const gchar *name, gpointer user_data) {
    AppState *state = user_data;
    GList *link = g_list_find_custom(state->mpris_players, name, (GCompareFunc)g_strcmp0);
    if (link) {
        g_free(link->data);
        state->mpris_players = g_list_delete_link(state->mpris_players, link);
        update_view(state);
    }
}

static void update_view(AppState *state) {
    gboolean has_players = (state->mpris_players != NULL);

    // Case 1: Players are active, but we are not showing the player view.
    if (has_players && state->mpris_state == NULL) {
        const gchar *bus_name = state->mpris_players->data;
        g_print("Player detected (%s). Creating player view.\n", bus_name);

        GtkWidget *new_view = create_mpris_view(bus_name, &state->mpris_state);
        if (new_view) {
            // Give the state a reference to the window it lives in.
            state->mpris_state->window = GTK_WINDOW(state->main_window);
            state->current_view = new_view;
            gtk_window_set_child(GTK_WINDOW(state->main_window), state->current_view);
        }
    } 
    // Case 2: No players are active, but we are showing the player view.
    else if (!has_players && state->mpris_state != NULL) {
        g_print("No active players. Reverting to default view.\n");
        // The mpris_state and its view will be destroyed automatically because
        // it was associated with the old widget via g_object_set_data_full.
        // When gtk_window_set_child unparents it, it gets destroyed.
        state->mpris_state = NULL; 
        state->current_view = create_default_view();
        gtk_window_set_child(GTK_WINDOW(state->main_window), state->current_view);
    }
}

static void on_name_owner_changed(GDBusConnection *c, const gchar *s, const gchar *o, const gchar *i, const gchar *sig, GVariant *p, gpointer d) {
    (void)c; (void)s; (void)o; (void)i; (void)sig;
    const gchar *name, *old_owner, *new_owner;
    g_variant_get(p, "(sss)", &name, &old_owner, &new_owner);
    if (name && g_str_has_prefix(name, "org.mpris.MediaPlayer2.")) {
        if (new_owner && *new_owner) { on_mpris_name_appeared(name, d); }
        else { on_mpris_name_vanished(name, d); }
    }
}

static void setup_mpris_watcher(AppState *state) {
    g_autoptr(GError) error = NULL;
    state->dbus_connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (error) { g_warning("DBus connection failed: %s", error->message); return; }

    g_autoptr(GVariant) result = g_dbus_connection_call_sync(state->dbus_connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", NULL, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (!error) {
        g_autoptr(GVariantIter) iter;
        g_variant_get(result, "(as)", &iter);
        gchar *name;
        while (g_variant_iter_loop(iter, "s", &name)) {
            if (g_str_has_prefix(name, "org.mpris.MediaPlayer2.")) { on_mpris_name_appeared(name, state); }
        }
    } else { g_warning("DBus ListNames failed: %s", error->message); }
    
    state->name_watcher_id = g_dbus_connection_signal_subscribe(state->dbus_connection, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus", NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_name_owner_changed, state, NULL);
    g_print("MPRIS watcher is active.\n");
}

static GtkWidget* create_default_view() {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

    GtkWidget *image = gtk_image_new_from_icon_name("audio-headphones-symbolic");
    gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_LARGE);
    gtk_image_set_pixel_size(GTK_IMAGE(image), 96);
    gtk_widget_add_css_class(image, "artist-label"); // Re-use style for grey color

    GtkWidget *label = gtk_label_new("No active player");
    gtk_widget_add_css_class(label, "title-label");

    gtk_box_append(GTK_BOX(box), image);
    gtk_box_append(GTK_BOX(box), label);

    return box;
}

static gboolean load_css(gpointer user_data) {
    AppState *state = user_data;
    GdkDisplay *display = gdk_display_get_default();
    if (state->css_provider) {
        gtk_style_context_remove_provider_for_display(display, GTK_STYLE_PROVIDER(state->css_provider));
    }
    state->css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(state->css_provider, "../src/style.css");
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(state->css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    return G_SOURCE_REMOVE;
}

static void on_css_changed(GFileMonitor *monitor, GFile *file, GFile *other, GFileMonitorEvent event, gpointer user_data) {
    (void)monitor; (void)file; (void)other;
    if (event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || event == G_FILE_MONITOR_EVENT_CHANGED) {
        g_idle_add(load_css, user_data);
    }
}
static void setup_css_hot_reload(AppState *state) {
    g_autoptr(GError) error = NULL;
    g_autoptr(GFile) css_file = g_file_new_for_path("../src/style.css");
    state->css_file_monitor = g_file_monitor_file(css_file, G_FILE_MONITOR_NONE, NULL, &error);
    if(error) { g_warning("CSS monitor failed: %s", error->message); return; }
    g_signal_connect(state->css_file_monitor, "changed", G_CALLBACK(on_css_changed), state);
}

static void cleanup(GApplication *app, gpointer user_data) {
    (void)app;
    AppState *state = (AppState*)user_data;
    if (state->name_watcher_id > 0) { g_dbus_connection_signal_unsubscribe(state->dbus_connection, state->name_watcher_id); }
    g_clear_object(&state->dbus_connection);
    g_list_free_full(state->mpris_players, g_free);
    if (state->css_file_monitor) { g_file_monitor_cancel(state->css_file_monitor); g_clear_object(&state->css_file_monitor); }
    g_print("Cleanup complete.\n");
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    
    AppState *state = g_new0(AppState, 1);
    state->app = app;
    g_object_set_data_full(G_OBJECT(app), "app-state", state, (GDestroyNotify)g_free);
    g_signal_connect(app, "shutdown", G_CALLBACK(cleanup), state);

    state->main_window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(state->main_window), 300, 450);
    gtk_widget_add_css_class(state->main_window, "mpris-popout");
    
    gtk_layer_init_for_window(GTK_WINDOW(state->main_window));
    gtk_layer_set_layer(GTK_WINDOW(state->main_window), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(state->main_window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(state->main_window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(state->main_window), GTK_LAYER_SHELL_EDGE_LEFT, HORIZONTAL_OFFSET);
    gtk_layer_set_margin(GTK_WINDOW(state->main_window), GTK_LAYER_SHELL_EDGE_TOP, PLAYER_VERTICAL_OFFSET);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(state->main_window), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    
    // Start with the default view
    state->current_view = create_default_view();
    gtk_window_set_child(GTK_WINDOW(state->main_window), state->current_view);
    
    gtk_window_present(GTK_WINDOW(state->main_window));
    
    g_idle_add(load_css, state);
    setup_css_hot_reload(state);
    setup_mpris_watcher(state);

    g_print("MPRIS Lyrics Viewer started.\n");
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.spn4x.mprislyrics", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}