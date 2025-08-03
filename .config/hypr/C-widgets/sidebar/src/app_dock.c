#include <gtk/gtk.h>
#include <gio/gio.h>
#include <graphene.h>
#include "app_dock.h"
#include "utils.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <sys/types.h>

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

// FORWARD DECLARATIONS
static void redraw_ui_from_map(void);
static void bootstrap_clients(void);

// HELPER & CALLBACK FUNCTIONS
static void free_client_info(gpointer data) { ClientInfo *c = data; if (!c) return; g_free(c->address); g_free(c->class_name); g_free(c); }
static void free_pinned_icon_data(gpointer data) { PinnedIconData *d = data; if (!d) return; g_free(d->class_name); g_free(d->exec_cmd); g_free(d); }
static void free_unpinned_icon_data(gpointer data) { UnpinnedIconData *d = data; if (!d) return; g_free(d->class_name); g_free(d); }
static void free_action_data(gpointer data, GClosure *closure) { (void)closure; ActionData *d = data; if (!d) return; g_free(d->class_name); g_free(d); }

static gchar* get_pinned_apps_path(void) { return g_strdup("../src/pinned.json"); }

static GList* load_pinned_apps(void) {
    g_autofree gchar *file_path = get_pinned_apps_path();
    if (!g_file_test(file_path, G_FILE_TEST_EXISTS)) return NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(JsonParser) parser = json_parser_new();
    if (!json_parser_load_from_file(parser, file_path, &error)) { return NULL; }
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
    if (!g_file_set_contents(get_pinned_apps_path(), str, -1, &error)) { g_warning("Failed to save pinned apps: %s", error->message); }
    g_object_unref(builder);
}

static void on_pinned_app_clicked(GtkButton *button, gpointer user_data) {
    (void)user_data; // This will be NULL, we ignore it.
    
    // Get the data directly from the widget itself. This is the safe way.
    PinnedIconData *data = g_object_get_data(G_OBJECT(button), "pinned-data");
    if (!data) return; // Should not happen, but good to be safe.

    char cmd[256];
    if (data->is_running)
        snprintf(cmd, sizeof(cmd), "hyprctl dispatch focuswindow class:^(%s)$", data->class_name);
    else
        snprintf(cmd, sizeof(cmd), "%s", data->exec_cmd);
    execute_command_async(cmd, NULL, NULL);
}

static void on_running_app_clicked(GtkButton *button, gpointer user_data) {
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

    // Properly remove existing popover
    if (app_dock_popover) {
        gtk_popover_popdown(GTK_POPOVER(app_dock_popover));
        gtk_widget_unparent(app_dock_popover);
        g_clear_object(&app_dock_popover);
    }

    GtkWidget *button = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkWidget *toplevel = gtk_widget_get_ancestor(button, GTK_TYPE_WINDOW);

    app_dock_popover = gtk_popover_new();
    gtk_widget_set_parent(app_dock_popover, toplevel);
    gtk_popover_set_autohide(GTK_POPOVER(app_dock_popover), TRUE);

    graphene_point_t top_left = { .x = 0, .y = 0 };
    graphene_point_t computed_point;
    gtk_widget_compute_point(button, toplevel, &top_left, &computed_point);

    GdkRectangle rect = {
        .x = computed_point.x,
        .y = computed_point.y,
        .width = gtk_widget_get_width(button),
        .height = gtk_widget_get_height(button)
    };
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

// CORE LOGIC
// src/app_dock.c

// CORE LOGIC
static void redraw_ui_from_map(void) {
    // Remove any existing popover cleanly
    if (app_dock_popover) {
        gtk_popover_popdown(GTK_POPOVER(app_dock_popover));
        gtk_widget_unparent(app_dock_popover);
        g_clear_object(&app_dock_popover);
    }

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(pinned_apps_container))) gtk_widget_unparent(child);
    while ((child = gtk_widget_get_first_child(running_apps_container))) gtk_widget_unparent(child);

    // --- Use a hash set for efficient lookup of pinned apps ---
    GHashTable *pinned_set = g_hash_table_new(g_str_hash, g_str_equal);
    GList *pinned_config = load_pinned_apps();
    for (GList *l = pinned_config; l; l = l->next) {
        PinnedIconData *pin_data = l->data;
        // The key is the class name, value is not important so we use the key itself.
        g_hash_table_add(pinned_set, pin_data->class_name);
    }

    // --- Draw Pinned Apps ---
    for (GList *l = pinned_config; l; l = l->next) {
        PinnedIconData *pin_data = l->data;
        if (!pin_data) continue; // Skip if data was already transferred

        pin_data->is_running = FALSE;
        GHashTableIter live_iter; gpointer key, value;
        g_hash_table_iter_init(&live_iter, clients_map);
        while (g_hash_table_iter_next(&live_iter, &key, &value)) {
            if (g_strcmp0(((ClientInfo*)value)->class_name, pin_data->class_name) == 0) {
                pin_data->is_running = TRUE;
                break;
            }
        }
        
        pin_data->button = gtk_button_new_from_icon_name(pin_data->class_name);
        if (pin_data->is_running) {
            gtk_widget_add_css_class(pin_data->button, "running-app");
        }
        
        // The button now takes full ownership of the pin_data struct.
        g_object_set_data_full(G_OBJECT(pin_data->button), "pinned-data", pin_data, (GDestroyNotify)free_pinned_icon_data);
        
        // Pass NULL as user_data. The handler will get data from the button.
        g_signal_connect(pin_data->button, "clicked", G_CALLBACK(on_pinned_app_clicked), NULL);
        
        GtkGesture *rc = g_object_new(GTK_TYPE_GESTURE_CLICK, "button", GDK_BUTTON_SECONDARY, NULL);
        g_signal_connect(rc, "pressed", G_CALLBACK(on_app_icon_right_clicked), NULL);
        gtk_widget_add_controller(pin_data->button, GTK_EVENT_CONTROLLER(rc));
        gtk_box_append(GTK_BOX(pinned_apps_container), pin_data->button);

        // We've transferred ownership of pin_data to the button, so we NULL out the
        // pointer in the list to prevent a double-free when the list is freed.
        l->data = NULL;
    }
    // This now safely frees only the list nodes, as the data pointers are NULL.
    g_list_free(pinned_config);


    // --- Draw Unpinned Running Apps ---
    GHashTableIter iter; gpointer key, value;
    g_hash_table_iter_init(&iter, clients_map);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ClientInfo *client = value;

        // EFFICIENT CHECK: If the app is in our pinned set, skip it.
        if (g_hash_table_contains(pinned_set, client->class_name)) {
            continue;
        }

        // This app is running but not pinned, so create a button for it.
        UnpinnedIconData *d = g_new0(UnpinnedIconData, 1);
        d->class_name = g_strdup(client->class_name); d->pid = client->pid;
        GtkWidget *btn = gtk_button_new_from_icon_name(d->class_name);
        gtk_widget_add_css_class(btn, "running-app");
        g_object_set_data_full(G_OBJECT(btn), "unpinned-data", d, free_unpinned_icon_data);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_running_app_clicked), d);
        GtkGesture *rc = g_object_new(GTK_TYPE_GESTURE_CLICK, "button", GDK_BUTTON_SECONDARY, NULL);
        g_signal_connect(rc, "pressed", G_CALLBACK(on_app_icon_right_clicked), NULL);
        gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(rc));
        gtk_box_append(GTK_BOX(running_apps_container), btn);
    }
    
    // Clean up the hash set.
    g_hash_table_destroy(pinned_set);

    // --- FINAL UI ADJUSTMENTS ---
    
    // Check if the running_apps_container has any children.
    GtkWidget *first_running_app = gtk_widget_get_first_child(running_apps_container);
    
    // Show the separator only if there is at least one unpinned running app.
    gtk_widget_set_visible(running_apps_separator, first_running_app != NULL);

    // Force the window to resize to fit the new content.
    // We get the top-level window by finding the ancestor of one of our containers.
    GtkWidget *toplevel = gtk_widget_get_ancestor(pinned_apps_container, GTK_TYPE_WINDOW);
    if (toplevel) {
        // Setting the default size to -1, -1 tells the window to use its
        // natural "preferred" size, which effectively makes it shrink-to-fit.
        gtk_window_set_default_size(GTK_WINDOW(toplevel), -1, -1);
    }
}

static void on_bootstrap_ready(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)source; (void)user_data;
    g_hash_table_remove_all(clients_map);
    const char *json = get_command_stdout(res);
    if (json) {
        g_autoptr(JsonParser) parser = json_parser_new();
        if (json_parser_load_from_data(parser, json, -1, NULL)) {
            JsonArray *arr = json_node_get_array(json_parser_get_root(parser));
            if (arr) {
                for (guint i = 0; i < json_array_get_length(arr); i++) {
                    JsonObject *obj = json_array_get_object_element(arr, i);
                    if (!json_object_get_boolean_member_with_default(obj, "hidden", FALSE)) {
                        const char* class_name = json_object_get_string_member(obj, "class");
                        if (class_name && *class_name) {
                            ClientInfo *client = g_new0(ClientInfo, 1);
                            client->address = g_strdup(json_object_get_string_member(obj, "address"));
                            client->class_name = g_strdup(class_name);
                            client->pid = (pid_t)json_object_get_int_member(obj, "pid");
                            g_hash_table_insert(clients_map, g_strdup(client->address), client);
                        }
                    }
                }
            }
        }
    }
    redraw_ui_from_map();
}

static void bootstrap_clients(void) {
    execute_command_async("hyprctl -j clients", on_bootstrap_ready, NULL);
}

static void process_ipc_event(const gchar* line) {
    if (!line) return;
    if (g_str_has_prefix(line, "openwindow>>")) {
        bootstrap_clients();
    } else if (g_str_has_prefix(line, "closewindow>>")) {
        const gchar *addr = line + strlen("closewindow>>");
        if (g_hash_table_remove(clients_map, addr)) redraw_ui_from_map();
    } else if (g_str_has_prefix(line, "activewindow>>") || g_str_has_prefix(line, "windowtitle>>") || g_str_has_prefix(line, "movewindow>>")) {
        bootstrap_clients();
    }
}

static void on_line_read(GObject *source, GAsyncResult *res, gpointer user_data);
static void start_line_reading(GDataInputStream *stream, GCancellable *cancellable) {
    g_data_input_stream_read_line_async(stream, 0, cancellable, on_line_read, g_object_ref(stream));
}
static void on_line_read(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)source;
    GDataInputStream *stream = G_DATA_INPUT_STREAM(user_data);
    g_autoptr(GError) error = NULL;
    g_autofree gchar *line = g_data_input_stream_read_line_finish(stream, res, NULL, &error);
    if (error) { g_warning("Hyprland socket error: %s", error->message); g_object_unref(stream); return; }
    if (!line) { g_info("Hyprland socket closed."); g_object_unref(stream); return; }
    process_ipc_event(line);
    start_line_reading(stream, NULL);
    g_object_unref(stream);
}

static void on_socket_connected(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)user_data;
    g_autoptr(GError) error = NULL;
    GSocketConnection *connection = g_socket_client_connect_finish(G_SOCKET_CLIENT(source), res, &error);
    if (error) { g_warning("Socket connection failed: %s. No live updates.", error->message); return; }
    g_info("Successfully connected to Hyprland socket for live events.");
    GInputStream *istream = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    g_autoptr(GDataInputStream) dstream = g_data_input_stream_new(istream);
    start_line_reading(dstream, NULL);
    g_object_unref(connection);
}

static void setup_hyprland_ipc_listener(void) {
    const char *instance_sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!instance_sig) { g_warning("HYPRLAND_INSTANCE_SIGNATURE not set. No live updates."); return; }
    g_autofree gchar *socket_path = g_build_filename(g_get_user_runtime_dir(), "hypr", instance_sig, ".socket2.sock", NULL);
    g_autoptr(GSocketClient) client = g_socket_client_new();
    g_autoptr(GSocketAddress) address = g_unix_socket_address_new(socket_path);
    g_socket_client_connect_async(client, G_SOCKET_CONNECTABLE(address), NULL, on_socket_connected, NULL);
}

static void on_gear_clicked(GtkButton *button, GtkStack *stack) { (void)button; gtk_stack_set_visible_child_name(stack, "controls"); }

GtkWidget* create_dock_view(GtkStack *stack) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_margin_top(box, 10);

    GtkWidget *gear = gtk_button_new_from_icon_name("emblem-system-symbolic");
    g_signal_connect(gear, "clicked", G_CALLBACK(on_gear_clicked), stack);
    gtk_box_append(GTK_BOX(box), gear);
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
    
    return box;
}
