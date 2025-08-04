// src/app_dock.c

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <graphene.h>
#include "app_dock.h"
#include "utils.h"
#include "mpris.h" // Now includes the MprisPopoutState struct
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h> // For mkdir

// POSITIONING: Tune the vertical position of the MPRIS popout here.
static const int MPRIS_POPOUT_VERTICAL_OFFSET = 100;

// DATA STRUCTURES
typedef struct { gchar *address; gchar *class_name; pid_t pid; } ClientInfo;
typedef struct { gchar *class_name; gchar *exec_cmd; gboolean is_running; GtkWidget *button; } PinnedIconData;
typedef struct { gchar *class_name; pid_t pid; } UnpinnedIconData;
typedef struct { gchar *class_name; pid_t pid; } ActionData;

// GLOBAL STATE
static GHashTable *clients_map = NULL;
static GtkWidget *pinned_apps_container = NULL;
static GtkWidget *running_apps_container = NULL;
static GtkWidget *app_dock_popover = NULL;
static GtkWidget *running_apps_separator = NULL;

// --- MPRIS PLAYER STATE ---
static GtkWidget *mpris_popout = NULL; // This pointer MUST be kept in sync.
static GtkWidget *mpris_button = NULL;
static GtkWidget *mpris_separator = NULL;
static GList *mpris_players = NULL; // List of active player bus names (gchar*)

// FORWARD DECLARATIONS
static void redraw_ui_from_map(void);
static void bootstrap_clients(void);
static void setup_mpris_watcher(void);
static void update_mpris_button_visibility(void);

// HELPER & CALLBACK FUNCTIONS
static void free_client_info(gpointer data) { ClientInfo *c = data; if (!c) return; g_free(c->address); g_free(c->class_name); g_free(c); }
static void free_pinned_icon_data(gpointer data) { PinnedIconData *d = data; if (!d) return; g_free(d->class_name); g_free(d->exec_cmd); g_free(d); }
// FIX: Correct signature for GClosureNotify to fix the compiler warning.
static void free_unpinned_icon_data(gpointer data, GClosure *closure) {
    (void)closure; // Silence unused parameter warning
    UnpinnedIconData *d = data; if (!d) return; g_free(d->class_name); g_free(d);
}
static void free_action_data(gpointer data, GClosure *closure) { (void)closure; ActionData *d = data; if (!d) return; g_free(d->class_name); g_free(d); }

static gchar* get_pinned_apps_path(void) {
    return g_build_filename(g_get_user_runtime_dir(), "hypr-sidebar", "pinned.json", NULL);
}

// --- ROBUST MPRIS WATCHER ---

static void on_mpris_popout_destroyed(GtkWidget *widget, gpointer user_data) {
    (void)widget; (void)user_data;
    if (mpris_popout) {
        g_print("MPRIS popout destroyed signal received. Nullifying global pointer.\n");
        mpris_popout = NULL;
    }
}

static void update_mpris_button_visibility(void) {
    gboolean has_players = (mpris_players != NULL);
    gtk_widget_set_visible(mpris_button, has_players);
    gtk_widget_set_visible(mpris_separator, has_players);
    if (!has_players && mpris_popout) {
        gtk_window_destroy(GTK_WINDOW(mpris_popout));
    }
}
static void on_mpris_name_appeared(GDBusConnection *c, const gchar *n, const gchar *o, gpointer d) {
    (void)c; (void)o; (void)d;
    if (g_list_find_custom(mpris_players, n, (GCompareFunc)g_strcmp0) == NULL) {
        g_print("MPRIS Player Appeared: %s\n", n);
        mpris_players = g_list_append(mpris_players, g_strdup(n));
        update_mpris_button_visibility();
    }
}
static void on_mpris_name_vanished(GDBusConnection *c, const gchar *n, gpointer d) {
    (void)c; (void)d;
    GList *link = g_list_find_custom(mpris_players, n, (GCompareFunc)g_strcmp0);
    if (link) {
        g_print("MPRIS Player Vanished: %s\n", (gchar*)link->data);
        if (mpris_popout) {
            MprisPopoutState *state = g_object_get_data(G_OBJECT(mpris_popout), "mpris-state");
            if (state && state->bus_name && g_strcmp0(state->bus_name, n) == 0) {
                 gtk_window_destroy(GTK_WINDOW(mpris_popout));
            }
        }
        g_free(link->data);
        mpris_players = g_list_delete_link(mpris_players, link);
        update_mpris_button_visibility();
    }
}
static void setup_mpris_watcher(void) {
    g_autoptr(GError) error = NULL;
    g_autoptr(GDBusConnection) con = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (error) { g_warning("Could not connect to session bus for MPRIS: %s", error->message); return; }
    g_autoptr(GVariant) res = g_dbus_connection_call_sync(con, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", NULL, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (error) { g_warning("Could not list D-Bus names for MPRIS scan: %s", error->message); } else {
        g_autoptr(GVariantIter) iter; g_variant_get(res, "(as)", &iter); gchar *name;
        while(g_variant_iter_loop(iter, "s", &name)) { if (g_str_has_prefix(name, "org.mpris.MediaPlayer2.")) on_mpris_name_appeared(con, name, NULL, NULL); }
    }
    const char *players_to_watch[] = {"spotify", "playerctld", "vlc", "brave", NULL};
    for (int i=0; players_to_watch[i]; ++i) {
        g_autofree gchar *bus = g_strdup_printf("org.mpris.MediaPlayer2.%s", players_to_watch[i]);
        g_bus_watch_name_on_connection(con, bus, G_BUS_NAME_WATCHER_FLAGS_NONE, on_mpris_name_appeared, on_mpris_name_vanished, NULL, NULL);
    }
}

static GList* load_pinned_apps(void) {
    g_autofree gchar *file_path = get_pinned_apps_path();
    if (!g_file_test(file_path, G_FILE_TEST_EXISTS)) return NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(JsonParser) parser = json_parser_new();
    if (!json_parser_load_from_file(parser, file_path, &error)) { g_warning("Failed to load pinned apps: %s", error->message); return NULL; }
    JsonArray *array = json_node_get_array(json_parser_get_root(parser));
    if (!array) return NULL;
    GList *pinned_list = NULL;
    for (guint i = 0; i < json_array_get_length(array); i++) {
        JsonObject *obj = json_array_get_object_element(array, i);
        PinnedIconData *info = g_new0(PinnedIconData, 1);
        info->class_name = g_strdup(json_object_get_string_member(obj, "class"));
        info->exec_cmd   = g_strdup(json_object_get_string_member(obj, "exec"));
        pinned_list = g_list_append(pinned_list, info);
    }
    return g_list_reverse(pinned_list);
}

static void save_pinned_apps(GList *pinned_list) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);
    for (GList *l = pinned_list; l; l = l->next) {
        PinnedIconData *info = l->data;
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "class"); json_builder_add_string_value(builder, info->class_name);
        json_builder_set_member_name(builder, "exec");  json_builder_add_string_value(builder, info->exec_cmd);
        json_builder_end_object(builder);
    }
    json_builder_end_array(builder);
    JsonNode *root = json_builder_get_root(builder);
    g_autoptr(JsonGenerator) generator = json_generator_new();
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);
    g_autofree gchar *str = json_generator_to_data(generator, NULL);
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = get_pinned_apps_path();
    g_autofree gchar *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    if (!g_file_set_contents(path, str, -1, &error)) { g_warning("Failed to save pinned apps: %s", error->message); }
    g_object_unref(builder);
}

static void on_pinned_app_clicked(GtkButton *button, gpointer user_data) {
    (void)user_data; 
    PinnedIconData *data = g_object_get_data(G_OBJECT(button), "pinned-data");
    if (!data) return;

    char cmd[256];
    if (data->is_running)
        snprintf(cmd, sizeof(cmd), "hyprctl dispatch focuswindow class:^(%s)$", data->class_name);
    else
        snprintf(cmd, sizeof(cmd), "%s", data->exec_cmd);
    execute_command_async(cmd, NULL, NULL);
}

static void on_running_app_clicked(GtkButton *button, gpointer user_data) {
    (void)button; 
    UnpinnedIconData *data = user_data; char cmd[256];
    snprintf(cmd, sizeof(cmd), "hyprctl dispatch focuswindow class:^(%s)$", data->class_name);
    execute_command_async(cmd, NULL, NULL);
}

static void on_unpin_app_clicked(GtkButton *popover_button, gpointer user_data) {
    (void)popover_button;
    ActionData *data = user_data;
    GList *list = load_pinned_apps();
    GList *found = NULL;
    for (GList *l = list; l; l = l->next) {
        if (g_strcmp0(((PinnedIconData*)l->data)->class_name, data->class_name) == 0) {
            found = l;
            break;
        }
    }
    if (found) { list = g_list_delete_link(list, found); save_pinned_apps(list); }
    g_list_free_full(list, free_pinned_icon_data);
    redraw_ui_from_map();
}

static void on_app_icon_right_clicked(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data) {
    (void)n_press; (void)x; (void)y; (void)user_data;
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) != GDK_BUTTON_SECONDARY) return;

    if (app_dock_popover) {
        gtk_popover_popdown(GTK_POPOVER(app_dock_popover));
        gtk_widget_unparent(app_dock_popover);
        app_dock_popover = NULL;
    }

    GtkWidget *button = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkWidget *toplevel = gtk_widget_get_ancestor(button, GTK_TYPE_WINDOW);

    app_dock_popover = gtk_popover_new();
    gtk_widget_set_parent(app_dock_popover, toplevel);
    gtk_popover_set_autohide(GTK_POPOVER(app_dock_popover), TRUE);

    graphene_rect_t bounds;
    // Cast to void to silence the unused result warning, as we don't need to check for failure here.
    (void)gtk_widget_compute_bounds(button, gtk_widget_get_parent(button), &bounds);
    
    GdkRectangle rect = { .x = bounds.origin.x, .y = bounds.origin.y, .width = bounds.size.width, .height = bounds.size.height };
    gtk_popover_set_pointing_to(GTK_POPOVER(app_dock_popover), &rect);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    PinnedIconData *pinned = g_object_get_data(G_OBJECT(button), "pinned-data");

    if (pinned) {
        GtkWidget *unpin_btn = gtk_button_new_with_label("Unpin");
        ActionData *action = g_new0(ActionData, 1);
        action->class_name = g_strdup(pinned->class_name);
        g_signal_connect_data(unpin_btn, "clicked", G_CALLBACK(on_unpin_app_clicked), action, (GClosureNotify)free_action_data, 0);
        gtk_box_append(GTK_BOX(box), unpin_btn);
    }
    gtk_popover_set_child(GTK_POPOVER(app_dock_popover), box);
    gtk_popover_popup(GTK_POPOVER(app_dock_popover));
}

static void redraw_ui_from_map(void) {
    if (app_dock_popover) {
        gtk_popover_popdown(GTK_POPOVER(app_dock_popover));
        gtk_widget_unparent(app_dock_popover);
        app_dock_popover = NULL;
    }
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(pinned_apps_container))) gtk_widget_unparent(child);
    while ((child = gtk_widget_get_first_child(running_apps_container))) gtk_widget_unparent(child);
    GHashTable *pinned_set = g_hash_table_new(g_str_hash, g_str_equal);
    GList *pinned_config = load_pinned_apps();
    for (GList *l = pinned_config; l; l = l->next) { PinnedIconData *pin_data = l->data; g_hash_table_add(pinned_set, pin_data->class_name); }
    for (GList *l = pinned_config; l; l = l->next) {
        PinnedIconData *pin_data = l->data; if (!pin_data) continue;
        pin_data->is_running = FALSE;
        GHashTableIter live_iter; gpointer key, value; g_hash_table_iter_init(&live_iter, clients_map);
        while (g_hash_table_iter_next(&live_iter, &key, &value)) { if (g_strcmp0(((ClientInfo*)value)->class_name, pin_data->class_name) == 0) { pin_data->is_running = TRUE; break; } }
        pin_data->button = gtk_button_new_from_icon_name(pin_data->class_name);
        if (pin_data->is_running) { gtk_widget_add_css_class(pin_data->button, "running-app"); }
        g_object_set_data_full(G_OBJECT(pin_data->button), "pinned-data", pin_data, (GDestroyNotify)free_pinned_icon_data);
        g_signal_connect(pin_data->button, "clicked", G_CALLBACK(on_pinned_app_clicked), NULL);
        GtkGesture *rc = g_object_new(GTK_TYPE_GESTURE_CLICK, "button", GDK_BUTTON_SECONDARY, NULL);
        g_signal_connect(rc, "pressed", G_CALLBACK(on_app_icon_right_clicked), NULL);
        gtk_widget_add_controller(pin_data->button, GTK_EVENT_CONTROLLER(rc));
        gtk_box_append(GTK_BOX(pinned_apps_container), pin_data->button);
        l->data = NULL;
    }
    g_list_free(pinned_config);
    GHashTableIter iter; gpointer key, value; g_hash_table_iter_init(&iter, clients_map);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ClientInfo *client = value;
        if (g_hash_table_contains(pinned_set, client->class_name)) { continue; }
        UnpinnedIconData *d = g_new0(UnpinnedIconData, 1); d->class_name = g_strdup(client->class_name); d->pid = client->pid;
        GtkWidget *btn = gtk_button_new_from_icon_name(d->class_name);
        gtk_widget_add_css_class(btn, "running-app");
        // FIX: The GClosureNotify cast now matches the function signature.
        g_signal_connect_data(btn, "clicked", G_CALLBACK(on_running_app_clicked), d, (GClosureNotify)free_unpinned_icon_data, 0);
        GtkGesture *rc = g_object_new(GTK_TYPE_GESTURE_CLICK, "button", GDK_BUTTON_SECONDARY, NULL);
        g_signal_connect(rc, "pressed", G_CALLBACK(on_app_icon_right_clicked), NULL);
        gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(rc));
        gtk_box_append(GTK_BOX(running_apps_container), btn);
    }
    g_hash_table_destroy(pinned_set);
    GtkWidget *first_pinned_app = gtk_widget_get_first_child(pinned_apps_container);
    GtkWidget *first_running_app = gtk_widget_get_first_child(running_apps_container);
    gtk_widget_set_visible(running_apps_separator, first_pinned_app != NULL && first_running_app != NULL);
    GtkWidget *toplevel = gtk_widget_get_ancestor(pinned_apps_container, GTK_TYPE_WINDOW);
    if (toplevel) { gtk_window_set_default_size(GTK_WINDOW(toplevel), -1, -1); }
}

static void on_bootstrap_ready(GObject *s, GAsyncResult *r, gpointer d) {
    (void)s; (void)d; g_hash_table_remove_all(clients_map); const char *json = get_command_stdout(r);
    if (json) {
        g_autoptr(JsonParser) p = json_parser_new();
        if (json_parser_load_from_data(p, json, -1, NULL)) {
            JsonArray *a = json_node_get_array(json_parser_get_root(p));
            if (a) {
                for (guint i = 0; i < json_array_get_length(a); i++) {
                    JsonObject *o = json_array_get_object_element(a, i);
                    if (!json_object_get_boolean_member_with_default(o, "hidden", FALSE)) {
                        const char* cn = json_object_get_string_member(o, "class");
                        if (cn && *cn) {
                            ClientInfo *c = g_new0(ClientInfo, 1); c->address = g_strdup(json_object_get_string_member(o, "address"));
                            c->class_name = g_strdup(cn); c->pid = (pid_t)json_object_get_int_member(o, "pid");
                            g_hash_table_insert(clients_map, g_strdup(c->address), c);
                        } } } } } } redraw_ui_from_map();
}

static void bootstrap_clients(void) { execute_command_async("hyprctl -j clients", on_bootstrap_ready, NULL); }

static void process_ipc_event(const gchar* line) {
    if (!line) return;
    if (g_str_has_prefix(line, "openwindow>>")) { bootstrap_clients(); }
    else if (g_str_has_prefix(line, "closewindow>>")) { const gchar *addr = line + strlen("closewindow>>"); if (g_hash_table_remove(clients_map, addr)) redraw_ui_from_map(); }
    else if (g_str_has_prefix(line, "activewindow>>") || g_str_has_prefix(line, "windowtitle>>") || g_str_has_prefix(line, "movewindow>>")) { bootstrap_clients(); }
}

static void on_line_read(GObject *s, GAsyncResult *r, gpointer d);

static void start_line_reading(GDataInputStream *s, GCancellable *c) {
    g_data_input_stream_read_line_async(s, 0, c, on_line_read, g_object_ref(s));
}

static void on_line_read(GObject *s, GAsyncResult *r, gpointer d) {
    (void)s; GDataInputStream *stream = G_DATA_INPUT_STREAM(d); g_autoptr(GError) error = NULL; g_autofree gchar *line = g_data_input_stream_read_line_finish(stream, r, NULL, &error);
    if (error) { g_warning("Hyprland socket error: %s", error->message); g_object_unref(stream); return; } if (!line) { g_info("Hyprland socket closed."); g_object_unref(stream); return; }
    process_ipc_event(line);
    start_line_reading(stream, NULL);
    g_object_unref(stream);
}

static void on_socket_connected(GObject *s, GAsyncResult *r, gpointer d) {
    (void)d; g_autoptr(GError) error = NULL; GSocketConnection *con = g_socket_client_connect_finish(G_SOCKET_CLIENT(s), r, &error);
    if (error) { g_warning("Socket connection failed: %s. No live updates.", error->message); return; } g_info("Connected to Hyprland socket.");
    GInputStream *is = g_io_stream_get_input_stream(G_IO_STREAM(con)); g_autoptr(GDataInputStream) ds = g_data_input_stream_new(is);
    start_line_reading(ds, NULL);
    g_object_unref(con);
}

static void setup_hyprland_ipc_listener(void) {
    const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE"); if (!sig) { g_warning("HYPRLAND_INSTANCE_SIGNATURE not set."); return; }
    g_autofree gchar *path = g_build_filename(g_get_user_runtime_dir(), "hypr", sig, ".socket2.sock", NULL);
    g_autoptr(GSocketClient) cli = g_socket_client_new(); g_autoptr(GSocketAddress) addr = g_unix_socket_address_new(path);
    g_socket_client_connect_async(cli, G_SOCKET_CONNECTABLE(addr), NULL, on_socket_connected, NULL);
}

static void on_gear_clicked(GtkButton *b, GtkStack *s) { (void)b; gtk_stack_set_visible_child_name(s, "controls"); }

static void on_player_icon_clicked(GtkButton *b, gpointer d) {
    if (mpris_popout) {
        gtk_window_destroy(GTK_WINDOW(mpris_popout));
        return;
    }
    
    if (!mpris_players) return; 
    const gchar* bus_name = mpris_players->data;
    GtkWidget *parent_window = gtk_widget_get_ancestor(GTK_WIDGET(b), GTK_TYPE_WINDOW);
    if (!parent_window) return;

    mpris_popout = create_mpris_popout(GTK_APPLICATION(d), parent_window, bus_name);
    
    g_signal_connect(mpris_popout, "destroy", G_CALLBACK(on_mpris_popout_destroyed), NULL);

    const int SIDEBAR_WIDTH = 66; const int X_ADJUST = -66;
    gtk_layer_init_for_window(GTK_WINDOW(mpris_popout));
    gtk_layer_set_layer(GTK_WINDOW(mpris_popout), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(mpris_popout), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(mpris_popout), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(mpris_popout), GTK_LAYER_SHELL_EDGE_LEFT, SIDEBAR_WIDTH + X_ADJUST);
    gtk_layer_set_margin(GTK_WINDOW(mpris_popout), GTK_LAYER_SHELL_EDGE_TOP, MPRIS_POPOUT_VERTICAL_OFFSET);
    gtk_window_present(GTK_WINDOW(mpris_popout));
}

GtkWidget* create_dock_view(GtkStack *stack) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_margin_top(box, 10);
    GtkApplication *app = GTK_APPLICATION(g_application_get_default());
    GtkWidget *gear = gtk_button_new_from_icon_name("emblem-system-symbolic");
    g_signal_connect(gear, "clicked", G_CALLBACK(on_gear_clicked), stack);
    gtk_box_append(GTK_BOX(box), gear);
    mpris_separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), mpris_separator);
    mpris_button = gtk_button_new_from_icon_name("media-playback-start-symbolic");
    g_signal_connect(mpris_button, "clicked", G_CALLBACK(on_player_icon_clicked), app);
    gtk_box_append(GTK_BOX(box), mpris_button);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    pinned_apps_container  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    running_apps_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(GTK_BOX(box), pinned_apps_container);
    running_apps_separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), running_apps_separator);
    gtk_box_append(GTK_BOX(box), running_apps_container);
    clients_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_client_info);
    bootstrap_clients();
    setup_hyprland_ipc_listener();
    setup_mpris_watcher();
    update_mpris_button_visibility();
    return box;
}