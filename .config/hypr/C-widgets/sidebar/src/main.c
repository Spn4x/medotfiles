// src/main.c

#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <json-glib/json-glib.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/param/format.h>
#include <spa/pod/builder.h>
#include <spa/param/audio/format-utils.h>

#include <fftw3.h>
#include <math.h>
#include <pthread.h>

#include "app_dock.h"
#include "control_center.h"

// Constants
#define RATE 48000
#define CHANNELS 2
#define SAMPLES 1024
#define NUM_BARS 16
#define CONFIG_FILE_PATH "config.json"

// App State Struct
typedef struct {
    GtkApplication *app;
    GtkWidget *sidebar_window;
    GtkWidget *handle_window;
    GtkWidget *drawing_area;
    GtkWidget *main_content_box;
    GtkWidget *stack;
    GtkWidget *idle_toggle_button;
    guint hide_timer_id;
     int current_sidebar_width;

    gboolean idle_mode_enabled;

    pthread_t pw_thread;
    struct pw_main_loop *pw_loop;
    struct pw_context *pw_context;
    struct pw_core *pw_core;
    struct pw_stream *pw_stream;
    struct spa_hook stream_listener;

    fftw_plan fft_plan;
    double *fft_in;
    fftw_complex *fft_out;

    pthread_mutex_t lock;
    double magnitudes[NUM_BARS];
    double peaks[NUM_BARS];
} AppState;

// --- Forward Declarations ---
static void cleanup(GApplication *app, gpointer user_data);
static gboolean trigger_redraw(gpointer user_data);
static void update_sidebar_for_idle_mode(AppState *state);
static void show_sidebar(AppState *state);
static void hide_sidebar(AppState *state);
static GtkWidget* create_sidebar_content(AppState *state);
// Updates the exclusive zone based on the sidebar's current width.
static gboolean set_exclusive_zone_from_content(gpointer user_data);

// --- JSON Config Management ---
// This section is correct and unchanged.
static void save_idle_mode_setting(gboolean is_enabled) {
    JsonObject *root_obj = json_object_new();
    json_object_set_boolean_member(root_obj, "idle_enabled", is_enabled);
    JsonGenerator *generator = json_generator_new();
    JsonNode *root_node = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(root_node, root_obj);
    json_generator_set_root(generator, root_node);
    json_generator_set_pretty(generator, TRUE);
    gchar *json_string = json_generator_to_data(generator, NULL);
    g_file_set_contents(CONFIG_FILE_PATH, json_string, -1, NULL);
    g_free(json_string);
    json_node_free(root_node);
    g_object_unref(generator);
    json_object_unref(root_obj);
}

static gboolean load_idle_mode_setting() {
    gboolean is_enabled = TRUE;
    if (g_file_test(CONFIG_FILE_PATH, G_FILE_TEST_EXISTS)) {
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_file(parser, CONFIG_FILE_PATH, NULL)) {
            JsonNode *root_node = json_parser_get_root(parser);
            if (JSON_NODE_HOLDS_OBJECT(root_node)) {
                JsonObject *root_obj = json_node_get_object(root_node);
                if (json_object_has_member(root_obj, "idle_enabled")) {
                    is_enabled = json_object_get_boolean_member(root_obj, "idle_enabled");
                }
            }
        }
        g_object_unref(parser);
    } else {
        save_idle_mode_setting(is_enabled);
    }
    return is_enabled;
}


// --- Audio, Drawing, PipeWire ---
// These sections are correct and unchanged.
static void on_pw_process(void *data) {
    AppState *state = (AppState*)data;
    struct pw_buffer *b;
    if ((b = pw_stream_dequeue_buffer(state->pw_stream)) == NULL) return;

    struct spa_buffer *buf = b->buffer;
    if (!buf->datas[0].data) {
        pw_stream_queue_buffer(state->pw_stream, b);
        return;
    }

    float *samples = buf->datas[0].data;
    uint32_t samples_count = buf->datas[0].chunk->size / (sizeof(float) * CHANNELS);
    for (uint32_t i = 0; i < samples_count && i < SAMPLES; i++) {
        state->fft_in[i] = (samples[i * CHANNELS] + samples[i * CHANNELS + 1]) / 2.0;
    }
    for (uint32_t i = samples_count; i < SAMPLES; i++) {
        state->fft_in[i] = 0.0;
    }

    fftw_execute(state->fft_plan);

    pthread_mutex_lock(&state->lock);
    for (int i = 0; i < NUM_BARS; i++) {
        double total_mag = 0;
        int start_freq = (i == 0) ? 1 : (SAMPLES / 2) * powf((float)i / NUM_BARS, 2.0f);
        int end_freq = (SAMPLES / 2) * powf((float)(i + 1) / NUM_BARS, 2.0f);
        for (int j = start_freq; j < end_freq && j < SAMPLES / 2; j++) {
            double re = state->fft_out[j][0], im = state->fft_out[j][1];
            total_mag += sqrt(re * re + im * im);
        }

        double db = 20 * log10(total_mag > 0 ? total_mag : 1);
        double normalized_db = (db < 0 ? 0 : db) / 60.0;
        double smoothed_val = pow(normalized_db, 1.5);

        if (smoothed_val > state->magnitudes[i]) {
            state->magnitudes[i] = smoothed_val;
        } else {
            state->magnitudes[i] *= 0.85;
        }

        if (state->magnitudes[i] > state->peaks[i]) {
            state->peaks[i] = state->magnitudes[i];
        } else {
            state->peaks[i] *= 0.99;
        }
    }
    pthread_mutex_unlock(&state->lock);

    g_idle_add(trigger_redraw, state);
    pw_stream_queue_buffer(state->pw_stream, b);
}
static gboolean trigger_redraw(gpointer user_data) {
    AppState *state = (AppState*)user_data;
    if (state && state->drawing_area) gtk_widget_queue_draw(state->drawing_area);
    return G_SOURCE_REMOVE;
}
static void draw_visualizer_bars(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer d) {
    (void)a;
    AppState *state = (AppState*)d;
    double bar_width = (double)w / NUM_BARS;
    pthread_mutex_lock(&state->lock);
    for (int i = 0; i < NUM_BARS; i++) {
        double mag = state->magnitudes[i] > 1.0 ? 1.0 : state->magnitudes[i];
        double bar_h = mag * h;
        if (bar_h < 1.0) bar_h = 0;
        cairo_pattern_t *pat = cairo_pattern_create_linear(0, h, 0, h - bar_h);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, 0.4, 0.9, 0.5, 0.9);
        cairo_pattern_add_color_stop_rgba(pat, 0.0, 0.1, 0.5, 0.2, 0.8);
        cairo_set_source(cr, pat);
        cairo_rectangle(cr, i * bar_width, h - bar_h, bar_width - 2.0, bar_h);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
        double peak_mag = state->peaks[i] > 1.0 ? 1.0 : state->peaks[i];
        double peak_h = peak_mag * h;
        if (peak_h > 1.0) {
            cairo_set_source_rgba(cr, 0.9, 0.9, 1.0, 0.8);
            cairo_rectangle(cr, i * bar_width, h - peak_h, bar_width - 2.0, 2.0);
            cairo_fill(cr);
        }
    }
    pthread_mutex_unlock(&state->lock);
}
static const struct pw_stream_events stream_events = { PW_VERSION_STREAM_EVENTS, .process = on_pw_process };
static void* pipewire_thread_func(void *data) { pw_main_loop_run(((AppState*)data)->pw_loop); return NULL; }
static void setup_pipewire(AppState *state) {
    state->pw_loop = pw_main_loop_new(NULL);
    state->pw_context = pw_context_new(pw_main_loop_get_loop(state->pw_loop), NULL, 0);
    state->pw_core = pw_context_connect(state->pw_context, NULL, 0);
    state->pw_stream = pw_stream_new(
        state->pw_core, "hypr-sidebar-visualizer",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                          PW_KEY_MEDIA_ROLE, "Visualizer", PW_KEY_STREAM_CAPTURE_SINK, "true", NULL));
    pw_stream_add_listener(state->pw_stream, &state->stream_listener, &stream_events, state);
    uint8_t buffer[1024]; struct spa_pod_builder b; spa_pod_builder_init(&b, buffer, sizeof(buffer));
    const struct spa_pod *params[1] = { spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &SPA_AUDIO_INFO_RAW_INIT(.format=SPA_AUDIO_FORMAT_F32, .channels=CHANNELS, .rate=RATE)) };
    pw_stream_connect(state->pw_stream, PW_DIRECTION_INPUT, PW_ID_ANY, PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS, params, 1);
    pthread_create(&state->pw_thread, NULL, pipewire_thread_func, state);
}


// --- App Logic ---

static gboolean set_exclusive_zone_from_content(gpointer user_data) {
    AppState *state = user_data;
    if (!state || !GTK_IS_WIDGET(state->sidebar_window) || !gtk_widget_get_visible(state->sidebar_window)) {
        return G_SOURCE_REMOVE;
    }
    
    // THE HARDCODED FIX: Use our reliable state variable.
    gtk_layer_set_exclusive_zone(GTK_WINDOW(state->sidebar_window), state->current_sidebar_width);
    
    return G_SOURCE_REMOVE;
}

static void show_sidebar(AppState *state) {
    if (!state->idle_mode_enabled) return;
    if (state->hide_timer_id > 0) { g_source_remove(state->hide_timer_id); state->hide_timer_id = 0; }

    gtk_layer_set_exclusive_zone(GTK_WINDOW(state->handle_window), 0);
    gtk_widget_set_visible(state->handle_window, FALSE);

    gtk_widget_set_visible(state->sidebar_window, TRUE);
    // Defer the zone calculation until the widget is drawn and has its new size.
    g_idle_add(set_exclusive_zone_from_content, state);
}

static void hide_sidebar(AppState *state) {
    if (!state->idle_mode_enabled) return;
    
    gtk_layer_set_exclusive_zone(GTK_WINDOW(state->sidebar_window), 0);
    gtk_widget_set_visible(state->sidebar_window, FALSE);
    
    gtk_layer_set_exclusive_zone(GTK_WINDOW(state->handle_window), 30);
    gtk_widget_set_visible(state->handle_window, TRUE);
}

static gboolean hide_sidebar_timeout(gpointer d) {
    hide_sidebar((AppState*)d);
    ((AppState*)d)->hide_timer_id = 0;
    return G_SOURCE_REMOVE;
}

static void on_handle_mouse_enter(GtkEventControllerMotion *c, gdouble x, gdouble y, gpointer d) { (void)c; (void)x; (void)y; show_sidebar((AppState*)d); }
static void on_sidebar_mouse_enter(GtkEventControllerMotion *c, gdouble x, gdouble y, gpointer d) { (void)c; (void)x; (void)y; show_sidebar((AppState*)d); }

static void on_sidebar_mouse_leave(GtkEventControllerMotion *c, gpointer d) {
    (void)c;
    AppState *state = d;
    if (!state->idle_mode_enabled) return;
    const char *current_page = gtk_stack_get_visible_child_name(GTK_STACK(state->stack));
    if (g_strcmp0(current_page, "dock") == 0) {
        if (state->hide_timer_id == 0) {
            state->hide_timer_id = g_timeout_add(500, hide_sidebar_timeout, d);
        }
    }
}

static void update_sidebar_for_idle_mode(AppState *state) {
   if (state->idle_mode_enabled) {
    gtk_widget_add_css_class(state->main_content_box, "floating-style");

    double x, y;
    GdkSurface *sidebar_surface =
        gtk_native_get_surface(GTK_NATIVE(state->sidebar_window));
    GdkDevice *pointer =
        gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default()));

    gboolean pointer_inside =
        gdk_surface_get_device_position(sidebar_surface, pointer, &x, &y, NULL);

    if (!pointer_inside) {
        hide_sidebar(state);
    }

    gtk_button_set_icon_name(GTK_BUTTON(state->idle_toggle_button), "view-pin-symbolic");
    gtk_widget_set_tooltip_text(state->idle_toggle_button, "Pin Sidebar");
}
 else {
        gtk_widget_remove_css_class(state->main_content_box, "floating-style");
        
        gtk_layer_set_exclusive_zone(GTK_WINDOW(state->handle_window), 0);
        gtk_widget_set_visible(state->handle_window, FALSE);
        
        gtk_widget_set_visible(state->sidebar_window, TRUE);
        // Defer the zone calculation until the widget is drawn.
        g_idle_add(set_exclusive_zone_from_content, state);
        
        gtk_button_set_icon_name(GTK_BUTTON(state->idle_toggle_button), "document-open-symbolic");
        gtk_widget_set_tooltip_text(state->idle_toggle_button, "Unpin Sidebar (Enable Idle Mode)");
    }
}

static void on_toggle_idle_mode_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *state = user_data;
    state->idle_mode_enabled = !state->idle_mode_enabled;
    save_idle_mode_setting(state->idle_mode_enabled);
    update_sidebar_for_idle_mode(state);
}

static void on_stack_child_changed(GtkStack *stack, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *state = user_data;

    // --- THE HARDCODED FIX ---
    // Define the widths for our views. You can adjust these values.
    const int DOCK_VIEW_WIDTH = 56; 
    const int CONTROLS_VIEW_WIDTH = 56; // This is also narrow

    // Get the name of the new visible page in the stack.
    const char *visible_page = gtk_stack_get_visible_child_name(stack);

    // Update our reliable state variable based on which view is showing.
    if (g_strcmp0(visible_page, "dock") == 0) {
        state->current_sidebar_width = DOCK_VIEW_WIDTH;
    } else {
        state->current_sidebar_width = CONTROLS_VIEW_WIDTH;
    }

    // If the sidebar is pinned and visible, schedule an update for the exclusive zone.
    if (!state->idle_mode_enabled && gtk_widget_get_visible(state->sidebar_window)) {
         g_idle_add(set_exclusive_zone_from_content, state);
    }
}

// --- GTK UI Setup ---
static void load_css(void) { GtkCssProvider *p = gtk_css_provider_new(); gtk_css_provider_load_from_path(p, "../src/style.css"); gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(p), 900); g_object_unref(p); }

static GtkWidget* create_sidebar_content(AppState *state) {
    state->main_content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(state->main_content_box, "main-content-box");

    state->stack = gtk_stack_new();
    gtk_stack_set_vhomogeneous(GTK_STACK(state->stack), FALSE);
    gtk_stack_set_hhomogeneous(GTK_STACK(state->stack), FALSE);
    gtk_stack_set_transition_type(GTK_STACK(state->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(state->stack), 350);

    g_signal_connect(state->stack, "notify::visible-child", G_CALLBACK(on_stack_child_changed), state);

    GtkWidget *dock_view = create_dock_view(GTK_STACK(state->stack));
    GtkWidget *cc_view = create_control_center_view(state->app, GTK_STACK(state->stack));

    state->idle_toggle_button = gtk_button_new_from_icon_name("view-pin-symbolic");
    gtk_widget_set_vexpand(state->idle_toggle_button, FALSE);
    g_signal_connect(state->idle_toggle_button, "clicked", G_CALLBACK(on_toggle_idle_mode_clicked), state);

    if (GTK_IS_BOX(cc_view)) {
        gtk_box_append(GTK_BOX(cc_view), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
        gtk_box_append(GTK_BOX(cc_view), state->idle_toggle_button);
    }
    
    gtk_stack_add_named(GTK_STACK(state->stack), dock_view, "dock");
    gtk_stack_add_named(GTK_STACK(state->stack), cc_view, "controls");

    gtk_box_append(GTK_BOX(state->main_content_box), state->stack);
    
    return state->main_content_box;
}

// --- only the 'activate' function is changed ---

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data; load_css(); AppState *state = g_new0(AppState, 1);
    state->app = app;
    g_object_set_data_full(G_OBJECT(app), "app-state", state, (GDestroyNotify)g_free);
    g_signal_connect(app, "shutdown", G_CALLBACK(cleanup), state);

       state->idle_mode_enabled = load_idle_mode_setting();
    state->current_sidebar_width = 66; // Initial width for the dock view.

    state->fft_in = fftw_alloc_real(SAMPLES);
    state->fft_out = fftw_alloc_complex(SAMPLES/2+1);
    state->fft_plan = fftw_plan_dft_r2c_1d(SAMPLES, state->fft_in, state->fft_out, FFTW_ESTIMATE);
    pthread_mutex_init(&state->lock, NULL);

    state->sidebar_window = gtk_application_window_new(app);
    gtk_layer_init_for_window(GTK_WINDOW(state->sidebar_window));
    gtk_layer_set_layer(GTK_WINDOW(state->sidebar_window), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(state->sidebar_window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);

    // THE FIX: Use the correct function 'gtk_layer_set_keyboard_mode'.
    // Setting the mode to GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND tells the
    // compositor that this surface can receive keyboard focus when requested
    // (e.g., when a user clicks a GtkDropDown). This is the key to allowing
    // popups to draw outside the main window's boundaries without being clipped.
    gtk_layer_set_keyboard_mode(GTK_WINDOW(state->sidebar_window), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    // The content, including the stack, is created here.
    gtk_window_set_child(GTK_WINDOW(state->sidebar_window), create_sidebar_content(state));

    GtkEventController *sm = gtk_event_controller_motion_new();
    g_signal_connect(sm, "enter", G_CALLBACK(on_sidebar_mouse_enter), state);
    g_signal_connect(sm, "leave", G_CALLBACK(on_sidebar_mouse_leave), state);
    gtk_widget_add_controller(state->sidebar_window, sm);

    state->handle_window = gtk_application_window_new(app);
    gtk_widget_add_css_class(state->handle_window, "handle-window");
    gtk_layer_init_for_window(GTK_WINDOW(state->handle_window));
    gtk_layer_set_layer(GTK_WINDOW(state->handle_window), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(state->handle_window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_window_set_default_size(GTK_WINDOW(state->handle_window), 30, -1);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(state->handle_window), 30);
    state->drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->drawing_area), draw_visualizer_bars, state, NULL);
    gtk_window_set_child(GTK_WINDOW(state->handle_window), state->drawing_area);
    GtkEventController *hm = gtk_event_controller_motion_new();
    g_signal_connect(hm, "enter", G_CALLBACK(on_handle_mouse_enter), state);
    gtk_widget_add_controller(state->handle_window, hm);

    update_sidebar_for_idle_mode(state);
    setup_pipewire(state);
}

// --- Main and Cleanup ---
static void cleanup(GApplication *app, gpointer user_data) {
    (void)app; AppState *state = (AppState*)user_data;
    if (state->pw_loop) pw_main_loop_quit(state->pw_loop);
    if (state->pw_thread) pthread_join(state->pw_thread, NULL);
    if (state->pw_stream) pw_stream_destroy(state->pw_stream);
    if (state->pw_core) pw_core_disconnect(state->pw_core);
    if (state->pw_context) pw_context_destroy(state->pw_context);
    if (state->pw_loop) pw_main_loop_destroy(state->pw_loop);
    if (state->fft_plan) fftw_destroy_plan(state->fft_plan);
    if (state->fft_in) fftw_free(state->fft_in);
    if (state->fft_out) fftw_free(state->fft_out);
    pthread_mutex_destroy(&state->lock);
}

int main(int argc, char **argv) {
    pw_init(&argc, &argv); GtkApplication *app = gtk_application_new("com.spn4x.hyprsidebar", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app); pw_deinit(); return status;
}