#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

// --- Configuration Constants ---

static const int BAR_WIDTH = 800;
static const int BAR_HEIGHT = 210;
static const int TOP_MARGIN = 50;
static const int PREVIEW_WIDTH = 220;
static const int PREVIEW_HEIGHT = 124;
static const int PREVIEW_SPACING = 20;


// --- Application Data Structure ---

typedef struct {
    GtkWindow *window;
    GtkBox *hbox;
    GtkScrolledWindow *scrolled_window;
    GList *previews;
    int selected_index;

    /** @brief A master cancellation token for all async operations. */
    GCancellable *cancellable;

} Application;


// --- Forward Declarations ---
static void app_update_view(Application *app);
static GtkWidget* ui_create_wallpaper_preview(Application *app, const char* path_str, int index);


// --- Core Logic & Event Handlers ---

static void app_select_and_quit(GtkWidget *preview_widget) {
    const char* path = g_object_get_data(G_OBJECT(preview_widget), "wallpaper-path");
    if (path) {
        g_print("%s\n", path);
        fflush(stdout);
    }
    gtk_main_quit();
}

static void ui_center_selected_item(Application *app) {
    if (g_list_length(app->previews) == 0) return;

    GtkWidget *selected_widget = g_list_nth_data(app->previews, app->selected_index);
    if (!selected_widget) return;

    GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(app->scrolled_window);

    int item_width = gtk_widget_get_allocated_width(selected_widget);
    int item_x;
    gtk_widget_translate_coordinates(selected_widget, GTK_WIDGET(app->hbox), 0, 0, &item_x, NULL);

    double viewport_width = gtk_adjustment_get_page_size(hadjustment);
    double new_scroll_value = item_x + (double)item_width / 2.0 - viewport_width / 2.0;

    double lower = gtk_adjustment_get_lower(hadjustment);
    double upper = gtk_adjustment_get_upper(hadjustment) - viewport_width;
    if (upper < lower) upper = lower;

    gtk_adjustment_set_value(hadjustment, CLAMP(new_scroll_value, lower, upper));
}

static void app_update_view(Application *app) {
    if (g_list_length(app->previews) == 0) return;

    for (GList *l = app->previews; l != NULL; l = g_list_next(l)) {
        GtkWidget *widget = l->data;
        GtkStyleContext *context = gtk_widget_get_style_context(widget);
        int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "widget-index"));
        if (index == app->selected_index) {
            gtk_style_context_add_class(context, "selected");
        } else {
            gtk_style_context_remove_class(context, "selected");
        }
    }

    GtkWidget *selected_widget = g_list_nth_data(app->previews, app->selected_index);
    if (selected_widget) {
        gtk_widget_grab_focus(selected_widget);
    }
    
    ui_center_selected_item(app);
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, Application *app) {
    int count = g_list_length(app->previews);
    if (count == 0) return FALSE;

    switch (event->keyval) {
        case GDK_KEY_Left:
        case GDK_KEY_h:
            app->selected_index = (app->selected_index - 1 + count) % count;
            app_update_view(app);
            return TRUE;

        case GDK_KEY_Right:
        case GDK_KEY_l:
            app->selected_index = (app->selected_index + 1) % count;
            app_update_view(app);
            return TRUE;

        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            {
                GtkWidget *selected = g_list_nth_data(app->previews, app->selected_index);
                if (selected) {
                    app_select_and_quit(selected);
                }
            }
            return TRUE;

        case GDK_KEY_Escape:
        case GDK_KEY_q:
            gtk_main_quit();
            return TRUE;
    }
    return FALSE;
}

static gboolean on_item_clicked(GtkWidget *event_box, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        app_select_and_quit(event_box);
    }
    return FALSE;
}


// --- Asynchronous Image Loading ---

static void load_image_thread_func(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable) {
    char *path = task_data;
    GError *error = NULL;

    // BEST PRACTICE: Check for cancellation *before* doing expensive I/O.
    if (g_cancellable_is_cancelled(cancellable)) {
        g_task_return_error(task, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CANCELLED, "Image loading cancelled on shutdown"));
        return;
    }
    
    GdkPixbuf *final_pixbuf = NULL;
    gboolean is_uncertain = FALSE;
    char *mime_type = g_content_type_guess(path, NULL, 0, &is_uncertain);
    if (mime_type && g_content_type_equals(mime_type, "image/gif")) {
        GdkPixbufAnimation *anim = gdk_pixbuf_animation_new_from_file(path, &error);
        if (anim) {
            GdkPixbuf *first_frame = gdk_pixbuf_animation_get_static_image(anim);
            if (first_frame) {
                final_pixbuf = gdk_pixbuf_scale_simple(first_frame, PREVIEW_WIDTH, PREVIEW_HEIGHT, GDK_INTERP_BILINEAR);
            }
            g_object_unref(anim);
        }
    }
    g_free(mime_type);

    if (final_pixbuf == NULL) {
        g_clear_error(&error);
        final_pixbuf = gdk_pixbuf_new_from_file_at_size(path, PREVIEW_WIDTH, PREVIEW_HEIGHT, &error);
    }

    if (error) {
        // g_warning("Failed to load image '%s': %s", path, error->message);
        g_task_return_error(task, g_error_copy(error));
        g_error_free(error);
        return;
    }

    g_task_return_pointer(task, final_pixbuf, g_object_unref);
}

static void on_image_loaded_cb(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GtkImage *image = GTK_IMAGE(user_data);
    GError *error = NULL;
    GdkPixbuf *pixbuf = g_task_propagate_pointer(G_TASK(res), &error);

    if (error) {
        // BEST PRACTICE: Don't warn on cancellation, it's an expected outcome.
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_warning("Async image load failed: %s", error->message);
        }
        g_error_free(error);
        return;
    }

    if (pixbuf) {
        gtk_image_set_from_pixbuf(image, pixbuf);
        g_object_unref(pixbuf);
    }
}

// --- UI Construction ---

static GtkWidget* ui_create_wallpaper_preview(Application *app, const char* path_str, int index) {
    GdkPixbuf *placeholder = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    gdk_pixbuf_fill(placeholder, 0x1E1E2EFF);
    GtkWidget *image = gtk_image_new_from_pixbuf(placeholder);
    gtk_widget_set_size_request(image, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    gtk_style_context_add_class(gtk_widget_get_style_context(image), "preview-image");
    g_object_unref(placeholder);

    GTask *task = g_task_new(NULL, app->cancellable, on_image_loaded_cb, image);
    g_task_set_task_data(task, g_strdup(path_str), g_free);
    g_task_run_in_thread(task, (GTaskThreadFunc)load_image_thread_func);
    g_object_unref(task);

    GtkWidget *label = gtk_label_new(g_path_get_basename(path_str));
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "filename-label");

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox), image, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    GtkWidget *event_box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(event_box), vbox);
    gtk_widget_set_can_focus(event_box, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(event_box), "preview-item");
    
    g_object_set_data_full(G_OBJECT(event_box), "wallpaper-path", g_strdup(path_str), g_free);
    g_object_set_data(G_OBJECT(event_box), "widget-index", GINT_TO_POINTER(index));
    g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_item_clicked), NULL);

    return event_box;
}

static void ui_load_css() {
    GtkCssProvider *provider = gtk_css_provider_new();
    
    // This gets the base path, e.g., "/home/youruser/.config"
    const char *config_dir = g_get_user_config_dir(); 

    // Now, we build the rest of your specific path on top of that base path.
    // Each part of the path is a separate argument.
    char *css_path = g_build_filename(config_dir, "hypr", "C-widgets", "archlauncher-c", "cachy.css", NULL);

    g_message("Attempting to load CSS from: %s", css_path); // A helpful debug message

    GError *error = NULL;
    if (!gtk_css_provider_load_from_path(provider, css_path, &error)) {
        // This fallback part is still very important!
        g_warning("Failed to load user CSS: %s. Using fallback style.", error->message);
        g_error_free(error);
        const char *fallback_css =
            "#main-window { background-color: rgba(30, 30, 46, 0.85); border-radius: 12px; border: 1px solid rgba(255,255,255,0.1); }"
            ".preview-item { border: 2px solid transparent; border-radius: 8px; padding: 8px; transition: all 0.2s ease-in-out; }"
            ".preview-item:focus { outline: none; }"
            ".preview-item.selected { background-color: rgba(203, 166, 247, 0.2); border-color: #cba6f7; }";
        gtk_css_provider_load_from_data(provider, fallback_css, -1, NULL);
    }

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    // Clean up the memory allocated by g_build_filename
    g_object_unref(provider);
    g_free(css_path);
}

static void ui_build(Application *app) {
    app->window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_widget_set_name(GTK_WIDGET(app->window), "main-window");

    gtk_layer_init_for_window(app->window);
    gtk_layer_set_layer(app->window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(app->window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_margin(app->window, GTK_LAYER_SHELL_EDGE_TOP, TOP_MARGIN);

    app->scrolled_window = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
    gtk_widget_set_name(GTK_WIDGET(app->scrolled_window), "scrolled-window");
    gtk_scrolled_window_set_policy(app->scrolled_window, GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_min_content_width(app->scrolled_window, BAR_WIDTH);
    gtk_widget_set_size_request(GTK_WIDGET(app->scrolled_window), BAR_WIDTH, BAR_HEIGHT);
    gtk_container_add(GTK_CONTAINER(app->window), GTK_WIDGET(app->scrolled_window));

    app->hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREVIEW_SPACING));
    gtk_widget_set_name(GTK_WIDGET(app->hbox), "main-hbox");
    gtk_widget_set_halign(GTK_WIDGET(app->hbox), GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(app->scrolled_window), GTK_WIDGET(app->hbox));

    ui_load_css();

    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(app->window, "key-press-event", G_CALLBACK(on_key_press), app);
}

static void app_populate_from_stdin(Application *app) {
    char buffer[4096];
    int index = 0;
    while (fgets(buffer, sizeof(buffer), stdin)) {
        buffer[strcspn(buffer, "\n")] = 0; // Strip newline
        if (buffer[0] == '\0') continue;
        if (!g_file_test(buffer, G_FILE_TEST_EXISTS)) {
            g_warning("File not found, skipping: %s", buffer);
            continue;
        }

        GtkWidget *preview = ui_create_wallpaper_preview(app, buffer, index++);
        gtk_box_pack_start(app->hbox, preview, FALSE, FALSE, 0);
        app->previews = g_list_append(app->previews, preview);
    }
}


// --- Application Lifecycle ---

Application* app_new() {
    Application *app = g_new0(Application, 1);
    app->selected_index = -1;
    app->cancellable = g_cancellable_new();
    return app;
}

void app_free(Application *app) {
    if (!app) return;

    // BEST PRACTICE: Cancel all pending async operations first.
    // This immediately signals all background threads to stop their work.
    g_cancellable_cancel(app->cancellable);
    
    // The widgets in the list are children of the main window, which GTK will
    // destroy and unref automatically when gtk_main_quit() is called.
    // We only need to free the list container itself.
    g_list_free(app->previews);
    
    g_object_unref(app->cancellable);
    g_free(app);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    Application *app = app_new();
    
    ui_build(app);
    app_populate_from_stdin(app);

    // BEST PRACTICE: Only run the app if there's something to show.
    if (g_list_length(app->previews) > 0) {
        gtk_widget_show_all(GTK_WIDGET(app->window));
        app->selected_index = 0;
        app_update_view(app);
        gtk_main();
    } else {
        g_warning("No valid image paths provided via stdin. Exiting.");
    }

    app_free(app);

    return 0;
}