#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <errno.h>
#include <glib/gstdio.h>

#define MAX_GLASSES 8.0
#define NUM_BUBBLES 15

typedef struct { double x_offset; double y; double radius; double speed; } Bubble;
typedef struct {
    GtkWidget *window;
    GtkWidget *drawing_area;
    GtkWidget *label;
    double current_glasses;
    double target_glasses;
    guint animation_timer_id;
    Bubble bubbles[NUM_BUBBLES];
} HydrateData;

void update_ui(HydrateData *data);
void save_state(HydrateData *data);

static const char* get_config_path() {
    static char path[256];
    const char *home = getenv("HOME");
    if (!home) return NULL;
    snprintf(path, sizeof(path), "%s/.config/hydrate-widget", home);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/.config/hydrate-widget/state", home);
    return path;
}

void load_state(HydrateData *data) {
    const char* path = get_config_path();
    if (!path) return;
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%lf", &data->current_glasses) == 1) {
            data->target_glasses = data->current_glasses;
        }
        fclose(f);
    }
}

void save_state(HydrateData *data) {
    const char* path = get_config_path();
    if (!path) return;
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%f", data->target_glasses);
        fclose(f);
    }
}

static void draw_rounded_rectangle(cairo_t *cr, double x, double y, double width, double height, double radius) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + radius, y + radius, radius, G_PI, 1.5 * G_PI);
    cairo_arc(cr, x + width - radius, y + radius, radius, 1.5 * G_PI, 2 * G_PI);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, 0.5 * G_PI);
    cairo_arc(cr, x + radius, y + height - radius, radius, 0.5 * G_PI, G_PI);
    cairo_close_path(cr);
}

static gboolean on_draw_area(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    HydrateData *data = (HydrateData *)user_data;
    guint width = gtk_widget_get_allocated_width(widget);
    guint height = gtk_widget_get_allocated_height(widget);
    const double BAR_WIDTH = 14.0;
    const double BAR_CORNER_RADIUS = 7.0;
    double center_x = width / 2.0;

    // Background trough
    cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 0.5);
    draw_rounded_rectangle(cr, center_x - BAR_WIDTH/2.0, 0, BAR_WIDTH, height, BAR_CORNER_RADIUS);
    cairo_fill(cr);

    double fill_fraction = data->current_glasses / MAX_GLASSES;
    if (fill_fraction < 0.0) fill_fraction = 0.0;
    if (fill_fraction > 1.0) fill_fraction = 1.0;
    double fill_height = height * fill_fraction;
    double fill_y = height - fill_height;

    // Green gradient fill
    cairo_pattern_t *gradient = cairo_pattern_create_linear(0, fill_y, 0, height);
    cairo_pattern_add_color_stop_rgba(gradient, 0.0, 0.290, 0.486, 0.255, 0.8);
    cairo_pattern_add_color_stop_rgba(gradient, 1.0, 0.235, 0.388, 0.208, 0.7);
    cairo_set_source(cr, gradient);
    draw_rounded_rectangle(cr, center_x - BAR_WIDTH/2.0, fill_y, BAR_WIDTH, fill_height, BAR_CORNER_RADIUS);
    cairo_fill(cr);
    cairo_pattern_destroy(gradient);

    // Bubbles
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
    for (int i = 0; i < NUM_BUBBLES; ++i) {
        double by = data->bubbles[i].y * fill_height;
        if (by < fill_height - data->bubbles[i].radius) {
            double bubble_x = center_x + data->bubbles[i].x_offset * (BAR_WIDTH / 3.0);
            double bubble_y = fill_y + by;
            cairo_arc(cr, bubble_x, bubble_y, data->bubbles[i].radius, 0, 2 * G_PI);
            cairo_fill(cr);
        }
    }
    return FALSE;
}

static gboolean animate_tick(gpointer user_data) {
    HydrateData *data = (HydrateData *)user_data;
    data->current_glasses += (data->target_glasses - data->current_glasses) * 0.1;
    for (int i = 0; i < NUM_BUBBLES; ++i) {
        data->bubbles[i].y -= data->bubbles[i].speed;
        if (data->bubbles[i].y < 0) {
            data->bubbles[i].y = 1.0;
            data->bubbles[i].x_offset = ((rand() % 200) / 100.0) - 1.0;
        }
    }
    update_ui(data);
    if (fabs(data->current_glasses - data->target_glasses) < 0.01) {
        data->current_glasses = data->target_glasses;
        data->animation_timer_id = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {
    HydrateData *data = (HydrateData *)user_data;
    if (event->direction == GDK_SCROLL_UP) data->target_glasses += 0.25;
    else if (event->direction == GDK_SCROLL_DOWN) data->target_glasses -= 0.25;
    if (data->target_glasses > MAX_GLASSES) data->target_glasses = MAX_GLASSES;
    if (data->target_glasses < 0.0) data->target_glasses = 0.0;
    if (data->animation_timer_id == 0)
        data->animation_timer_id = g_timeout_add(16, animate_tick, data);
    return TRUE;
}

void update_ui(HydrateData *data) {
    char buffer[32];
    if (data->target_glasses >= MAX_GLASSES) g_snprintf(buffer, sizeof(buffer), "Full!");
    else g_snprintf(buffer, sizeof(buffer), "%.1f", data->target_glasses);
    gtk_label_set_text(GTK_LABEL(data->label), buffer);
    gtk_widget_queue_draw(data->drawing_area);
}

static void on_destroy(GtkWidget *widget, gpointer user_data) {
    save_state((HydrateData *)user_data);
    gtk_main_quit();
}

static void load_css(const char* executable_path) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gchar *dir = g_path_get_dirname(executable_path);
    gchar *css_path = g_build_filename(dir, "style.css", NULL);
    if (g_file_test(css_path, G_FILE_TEST_EXISTS))
        gtk_css_provider_load_from_path(provider, css_path, NULL);
    else {
        const char *fallback = "window{background-color:transparent}"\
                              ".main-container{background-color:rgba(20,20,30,0.8);border-radius:15px;"\
                              "border:1px solid rgba(255,255,255,0.1)}"\
                              ".info-label{color:#ddeeff;font-weight:bold;font-size:14px}";
        gtk_css_provider_load_from_data(provider, fallback, -1, NULL);
    }
    g_free(dir); g_free(css_path);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    srand(time(NULL));

    const int WIDGET_WIDTH = 80;
    const int WIDGET_HEIGHT = 250;
    const gboolean ENABLE_BUBBLES = TRUE;

    HydrateData data = {0};
    // default start at 5 glasses if no previous state
    data.current_glasses = 55.0;
    data.target_glasses = 55.0;
    if (ENABLE_BUBBLES)
        for (int i = 0; i < NUM_BUBBLES; ++i)
            data.bubbles[i] = ((Bubble){((rand()%200)/100.0)-1.0, (rand()%100)/100.0, (rand()%3)+1.0, ((rand()%30)/10000.0)+0.0005});

    load_state(&data);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    data.window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Hydrate");
    gtk_widget_set_size_request(window, WIDGET_WIDTH, WIDGET_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, 10);
    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, 10);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    gtk_widget_set_app_paintable(window, TRUE);
    GdkVisual *visual = gdk_screen_get_rgba_visual(gtk_widget_get_screen(window));
    if (visual) gtk_widget_set_visual(window, visual);

    // Enable scroll on window too
    gtk_widget_add_events(window, GDK_SCROLL_MASK);
    g_signal_connect(window, "scroll-event", G_CALLBACK(on_scroll), &data);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_container_add(GTK_CONTAINER(window), grid);
    gtk_widget_set_valign(grid, GTK_ALIGN_FILL);
    gtk_style_context_add_class(gtk_widget_get_style_context(grid), "main-container");

    data.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(data.drawing_area, TRUE);
    gtk_widget_set_vexpand(data.drawing_area, TRUE);
    gtk_widget_set_can_focus(data.drawing_area, TRUE);
    gtk_widget_add_events(data.drawing_area, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(data.drawing_area, "scroll-event", G_CALLBACK(on_scroll), &data);

    data.label = gtk_label_new("");
    gtk_widget_set_halign(data.label, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(data.label), "info-label");

    gtk_grid_attach(GTK_GRID(grid), data.drawing_area, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), data.label, 0, 1, 1, 1);

    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), &data);
    g_signal_connect(data.drawing_area, "draw", G_CALLBACK(on_draw_area), &data);

    load_css(argv[0]);
    update_ui(&data);
    gtk_widget_show_all(window);

    // Grab focus so scroll works
    gtk_widget_grab_focus(data.drawing_area);

    gtk_main();
    return 0;
}
