#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <fontconfig/fontconfig.h>

// -----------------------------------------------------------------------------
// Constants and Type Definitions
// -----------------------------------------------------------------------------

#define WINDOW_WIDTH 350
#define WINDOW_HEIGHT 400
#define TOP_MARGIN 6
#define CACHE_DIR_NAME "cachy"
#define RUN_CACHE_FILE "run_cache.txt"

// A struct to hold our simplified application info
typedef struct {
    char *name;
    char *exec;
    GIcon *icon;
} AppInfo;

// The mode of operation (application launcher or command runner)
typedef enum {
    MODE_DRUN,
    MODE_RUN
} LauncherMode;

// A struct to hold all the state and widgets for our application
typedef struct {
    GtkWindow *window;
    GtkEntry *entry;
    GtkListBox *list_box;
    GSList *apps;
    LauncherMode mode;
    gboolean no_icons;
    gboolean rebuild_cache; // <<< FIX: Flag to force cache rebuild
} LauncherData;

// -----------------------------------------------------------------------------
// Forward Declarations (Prototypes)
// -----------------------------------------------------------------------------

void launch_selected_app(LauncherData *data);
void navigate_list(LauncherData *data, gint direction);
gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
void on_search_changed(GtkEntry *entry, gpointer user_data);
void on_launch_app(GtkListBox *box, GtkListBoxRow *row, gpointer user_data);
void on_entry_activate(GtkEntry *entry, gpointer user_data);
GSList* get_applications(gboolean no_icons);
// <<< FIX: Signature changed to accept rebuild_cache flag
GSList* get_run_executables(gboolean no_icons, gboolean rebuild_cache);
void create_launcher_window(LauncherData *data);
gboolean populate_list(gpointer user_data);
void load_css();

// -----------------------------------------------------------------------------
// Helper and Utility Functions
// -----------------------------------------------------------------------------

// Frees the memory associated with an AppInfo struct
void free_app_info(gpointer data) {
    AppInfo *app = (AppInfo *)data;
    g_free(app->name);
    g_free(app->exec);
    if (app->icon) {
        g_object_unref(app->icon);
    }
    g_free(app);
}

// Comparison function for sorting AppInfo structs alphabetically by name
gint compare_apps(gconstpointer a, gconstpointer b) {
    AppInfo *app_a = (AppInfo *)a;
    AppInfo *app_b = (AppInfo *)b;
    return g_strcmp0(app_a->name, app_b->name);
}

// Scans for and returns a list of all .desktop applications
GSList* get_applications(gboolean no_icons) {
    GSList *apps = NULL;
    GList *app_infos = g_app_info_get_all();

    for (GList *l = app_infos; l != NULL; l = l->next) {
        GAppInfo *app_info = G_APP_INFO(l->data);
        if (g_app_info_should_show(app_info)) {
            const gchar *name = g_app_info_get_display_name(app_info);
            const gchar *exec = g_app_info_get_commandline(app_info);
            if (!name || !exec) {
                continue;
            }

            AppInfo *app = g_new(AppInfo, 1);
            app->name = g_strdup(name);
            app->exec = g_strdup(exec);

            if (no_icons) {
                app->icon = NULL;
            } else {
                app->icon = g_app_info_get_icon(app_info);
                if (app->icon) {
                    g_object_ref(app->icon);
                }
            }
            apps = g_slist_prepend(apps, app);
        }
    }
    g_list_free_full(app_infos, g_object_unref);
    apps = g_slist_sort(apps, compare_apps);
    return apps;
}

// <<< FIX: THIS ENTIRE FUNCTION IS REWORKED FOR CACHING >>>
// Scans for executables in $PATH, using a cache for speed.
GSList* get_run_executables(gboolean no_icons, gboolean rebuild_cache) {
    GSList *apps = NULL;

    // 1. Determine the cache file path (~/.cache/cachy/run_cache.txt)
    const gchar *cache_dir = g_get_user_cache_dir();
    gchar *cache_path = g_build_filename(cache_dir, CACHE_DIR_NAME, RUN_CACHE_FILE, NULL);

    // 2. Decide if we need to rebuild the cache
    gboolean do_rebuild = rebuild_cache || !g_file_test(cache_path, G_FILE_TEST_EXISTS);

    if (do_rebuild) {
        // --- SLOW PATH: Scan filesystem and write to cache ---
        g_debug("Rebuilding RUN cache at: %s", cache_path);
        const gchar *path_env = g_getenv("PATH");
        if (!path_env) {
             g_free(cache_path);
             return NULL;
        }

        gchar **paths = g_strsplit(path_env, ":", 0);
        GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        GString *cache_content = g_string_new("");

        for (int i = 0; paths[i] != NULL; i++) {
            GDir *dir = g_dir_open(paths[i], 0, NULL);
            if (!dir) continue;

            const gchar *filename;
            while ((filename = g_dir_read_name(dir))) {
                if (filename[0] == '.' || g_hash_table_contains(seen, filename)) {
                    continue;
                }
                gchar *full_path = g_build_filename(paths[i], filename, NULL);
                if (g_file_test(full_path, G_FILE_TEST_IS_EXECUTABLE) && !g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
                    g_hash_table_add(seen, g_strdup(filename));
                    // Also add to our cache string for saving later
                    g_string_append_printf(cache_content, "%s\n", filename);
                }
                g_free(full_path);
            }
            g_dir_close(dir);
        }

        // Save the collected names to the cache file
        gchar *cache_dir_path = g_path_get_dirname(cache_path);
        g_mkdir_with_parents(cache_dir_path, 0755);
        g_file_set_contents(cache_path, cache_content->str, -1, NULL);
        g_free(cache_dir_path);
        g_string_free(cache_content, TRUE);

        // Now, create the AppInfo list from the 'seen' hash table for the current session
        GList* keys = g_hash_table_get_keys(seen);
        for(GList* l = keys; l != NULL; l = l->next) {
            const gchar* filename = l->data;
            AppInfo *app = g_new(AppInfo, 1);
            app->name = g_strdup(filename);
            app->exec = g_strdup(filename);
            app->icon = NULL; // Icon is set later
            apps = g_slist_prepend(apps, app);
        }
        g_list_free(keys);
        g_hash_table_destroy(seen);
        g_strfreev(paths);

    } else {
        // --- FAST PATH: Read from cache file ---
        g_debug("Reading RUN cache from: %s", cache_path);
        gchar *contents;
        gsize length;
        if (g_file_get_contents(cache_path, &contents, &length, NULL)) {
            gchar **lines = g_strsplit(contents, "\n", -1);
            for (int i = 0; lines[i] != NULL; i++) {
                if (lines[i][0] == '\0') continue; // Skip empty lines
                AppInfo *app = g_new(AppInfo, 1);
                app->name = g_strdup(lines[i]);
                app->exec = g_strdup(lines[i]);
                app->icon = NULL; // Icon is set later
                apps = g_slist_prepend(apps, app);
            }
            g_strfreev(lines);
            g_free(contents);
        }
    }
    g_free(cache_path);

    // 3. Sort and apply icons (common to both paths)
    apps = g_slist_sort(apps, compare_apps);
    if (!no_icons) {
        GIcon *generic_icon = g_themed_icon_new("utilities-terminal");
        for (GSList *l = apps; l != NULL; l = l->next) {
            AppInfo *app = (AppInfo *)l->data;
            app->icon = g_object_ref(generic_icon);
        }
        g_object_unref(generic_icon);
    }
    return apps;
}

// Loads custom CSS for styling the application
void load_css() {
    GtkCssProvider *provider = gtk_css_provider_new();
    GdkDisplay *display = gdk_display_get_default();
    GdkScreen *screen = gdk_display_get_default_screen(display);
    gtk_css_provider_load_from_path(provider, "launcher.css", NULL);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

// Launches the currently selected application in the list box
void launch_selected_app(LauncherData *data) {
    GtkListBoxRow *selected_row = gtk_list_box_get_selected_row(data->list_box);
    if (!selected_row) return;

    AppInfo *app = g_object_get_data(G_OBJECT(selected_row), "app-info");
    if (!app) return;

    gchar **parts = g_strsplit(app->exec, "%", 2);
    gchar *cmd = g_strstrip(parts[0]);
    gchar **argv = NULL;
    GError *error = NULL;

    if (g_shell_parse_argv(cmd, NULL, &argv, &error)) {
        g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, NULL, &error);
    }

    if (error) {
        g_warning("Failed to launch application: %s", error->message);
        g_error_free(error);
    }

    g_strfreev(argv);
    g_strfreev(parts);
    gtk_main_quit();
}

// Navigates the listbox up or down and ensures the selected item is visible
void navigate_list(LauncherData *data, gint direction) {
    GtkListBoxRow *selected_row = gtk_list_box_get_selected_row(data->list_box);
    if (!selected_row) return;

    GList *all_rows = gtk_container_get_children(GTK_CONTAINER(data->list_box));
    GSList *visible_rows = NULL;
    for (GList *l = all_rows; l != NULL; l = l->next) {
        if (gtk_widget_is_visible(l->data)) {
            visible_rows = g_slist_prepend(visible_rows, l->data);
        }
    }
    g_list_free(all_rows);
    visible_rows = g_slist_reverse(visible_rows);
    if (!visible_rows) return;

    gint index = g_slist_index(visible_rows, selected_row);
    if (index != -1) {
        gint len = g_slist_length(visible_rows);
        gint new_index = (index + direction + len) % len;
        GtkListBoxRow *new_row = g_slist_nth_data(visible_rows, new_index);

        if (new_row) {
            gtk_list_box_select_row(data->list_box, new_row);
            gtk_widget_grab_focus(GTK_WIDGET(new_row));
            gtk_widget_grab_focus(GTK_WIDGET(data->entry));
        }
    }
    g_slist_free(visible_rows);
}

// -----------------------------------------------------------------------------
// GTK Callbacks and Main Logic
// -----------------------------------------------------------------------------

gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    LauncherData *data = (LauncherData *)user_data;
    switch (event->keyval) {
        case GDK_KEY_Escape:
            gtk_main_quit();
            return TRUE;
        case GDK_KEY_Down:
            navigate_list(data, 1);
            return TRUE;
        case GDK_KEY_Up:
            navigate_list(data, -1);
            return TRUE;
        default:
            return FALSE;
    }
}

void on_search_changed(GtkEntry *entry, gpointer user_data) {
    LauncherData *data = (LauncherData *)user_data;
    const gchar *search_text = gtk_entry_get_text(entry);
    gchar *search_lower = g_utf8_strdown(search_text, -1);

    GList *children = gtk_container_get_children(GTK_CONTAINER(data->list_box));
    gboolean has_visible_rows = FALSE;

    for (GList *l = children; l != NULL; l = l->next) {
        GtkWidget *row = l->data;
        AppInfo *app = g_object_get_data(G_OBJECT(row), "app-info");
        if (!app) continue;

        gchar *name_lower = g_utf8_strdown(app->name, -1);
        if (g_strrstr(name_lower, search_lower)) {
            gtk_widget_show(row);
            has_visible_rows = TRUE;
        } else {
            gtk_widget_hide(row);
        }
        g_free(name_lower);
    }
    g_list_free(children);
    g_free(search_lower);

    if (has_visible_rows) {
        GtkListBoxRow *selected_row = gtk_list_box_get_selected_row(data->list_box);
        if (!selected_row || !gtk_widget_is_visible(GTK_WIDGET(selected_row))) {
            GList *all_rows = gtk_container_get_children(GTK_CONTAINER(data->list_box));
            for (GList *l = all_rows; l != NULL; l = l->next) {
                if (gtk_widget_is_visible(l->data)) {
                    gtk_list_box_select_row(data->list_box, GTK_LIST_BOX_ROW(l->data));
                    break;
                }
            }
            g_list_free(all_rows);
        }
    }
}

void on_launch_app(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    LauncherData *data = (LauncherData *)user_data;
    launch_selected_app(data);
}

void on_entry_activate(GtkEntry *entry, gpointer user_data) {
    LauncherData *data = (LauncherData *)user_data;
    launch_selected_app(data);
}

gboolean populate_list(gpointer user_data) {
    LauncherData *data = (LauncherData *)user_data;

    if (data->mode == MODE_RUN) {
        // <<< FIX: Pass the rebuild_cache flag to the function
        data->apps = get_run_executables(data->no_icons, data->rebuild_cache);
    } else {
        data->apps = get_applications(data->no_icons);
    }

    if (!data->apps) {
        g_printerr("No items found for the selected mode.\n");
        return G_SOURCE_REMOVE;
    }

    for (GSList *l = data->apps; l != NULL; l = l->next) {
        AppInfo *app = (AppInfo *)l->data;
        GtkWidget *row = gtk_list_box_row_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(row), "app-row");
        g_object_set_data(G_OBJECT(row), "app-info", app);

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_style_context_add_class(gtk_widget_get_style_context(box), "app-row-box");

        GtkWidget *icon = gtk_image_new_from_gicon(app->icon, GTK_ICON_SIZE_DIALOG);
        gtk_style_context_add_class(gtk_widget_get_style_context(icon), "app-icon");

        GtkWidget *label = gtk_label_new(app->name);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_style_context_add_class(gtk_widget_get_style_context(label), "app-name");

        gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_list_box_insert(data->list_box, row, -1);
    }

    gtk_widget_show_all(GTK_WIDGET(data->list_box));
    GtkListBoxRow *first_row = gtk_list_box_get_row_at_index(data->list_box, 0);
    if (first_row) {
        gtk_list_box_select_row(data->list_box, first_row);
    }

    return G_SOURCE_REMOVE;
}

void create_launcher_window(LauncherData *data) {
    data->window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->window)), "launcher-window");
    gtk_window_set_default_size(data->window, WINDOW_WIDTH, WINDOW_HEIGHT);
    gtk_window_set_decorated(GTK_WINDOW(data->window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(data->window), FALSE);

    gtk_layer_init_for_window(data->window);
    gtk_layer_set_layer(data->window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(data->window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    gtk_layer_set_anchor(data->window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_margin(data->window, GTK_LAYER_SHELL_EDGE_TOP, TOP_MARGIN);

    GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(data->window));
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual != NULL) gtk_widget_set_visual(GTK_WIDGET(data->window), visual);
    gtk_widget_set_app_paintable(GTK_WIDGET(data->window), TRUE);

    GtkWidget *main_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(main_container), "main-container");
    gtk_container_add(GTK_CONTAINER(data->window), main_container);

    data->entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(data->entry, "Search...");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->entry)), "input-entry");
    gtk_box_pack_start(GTK_BOX(main_container), GTK_WIDGET(data->entry), FALSE, FALSE, 0);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_app_paintable(scrolled_window, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_style_context_add_class(gtk_widget_get_style_context(scrolled_window), "scrolled-window");
    gtk_box_pack_start(GTK_BOX(main_container), scrolled_window, TRUE, TRUE, 0);

    data->list_box = GTK_LIST_BOX(gtk_list_box_new());
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->list_box)), "app-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(data->list_box), GTK_SELECTION_SINGLE);
    gtk_container_add(GTK_CONTAINER(scrolled_window), GTK_WIDGET(data->list_box));

    g_signal_connect(data->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(data->window, "key-press-event", G_CALLBACK(on_key_press), data);
    g_signal_connect(data->entry, "changed", G_CALLBACK(on_search_changed), data);
    g_signal_connect(data->entry, "activate", G_CALLBACK(on_entry_activate), data);
    g_signal_connect(data->list_box, "row-activated", G_CALLBACK(on_launch_app), data);
}

// -----------------------------------------------------------------------------
// Main Function
// -----------------------------------------------------------------------------

int main(int argc, char *argv[]) {
    FcInit();

    // <<< FIX: Argument parsing updated for --rebuild-cache >>>
    LauncherData *data = g_new0(LauncherData, 1);
    data->mode = MODE_DRUN;
    data->no_icons = FALSE;
    data->rebuild_cache = FALSE;
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "run") == 0) {
            data->mode = MODE_RUN;
        } else if (g_strcmp0(argv[i], "--no-icons") == 0) {
            data->no_icons = TRUE;
        } else if (g_strcmp0(argv[i], "--rebuild-cache") == 0) {
            data->rebuild_cache = TRUE;
        }
    }

    gtk_init(&argc, &argv);
    load_css();
    create_launcher_window(data);

    gtk_widget_show_all(GTK_WIDGET(data->window));
    gtk_widget_grab_focus(GTK_WIDGET(data->entry));

    g_idle_add(populate_list, data);

    gtk_main();

    g_slist_free_full(data->apps, free_app_info);
    g_free(data);

    return 0;
}