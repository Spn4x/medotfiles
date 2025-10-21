#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <json-glib/json-glib.h>
#include <stdio.h>
#include <string.h>

// --- AppState struct updated for the animation ---
typedef struct {
    GtkApplication *app;
    GtkWindow *main_window;
    GtkWidget *main_container;      // This will be our "island"
    GtkWidget *schedule_grid;
    GtkWidget *edit_popover;
    GtkWidget *settings_popover;
    JsonNode *schedule_data;
    char *config_file_path;
    GtkCssProvider *css_provider;
    GFileMonitor *css_monitor;
    GtkSizeGroup *time_slot_size_group;

    // --- New widgets for the animation ---
    GtkWidget *pill_label;          // The initial "Schedule" label
    GtkWidget *content_revealer;    // Reveals the full schedule
} AppState;


// Forward declarations of all functions
static void on_activate(GtkApplication *app);
static void build_ui(AppState *state);
static void load_or_create_schedule_data(AppState *state);
static void build_schedule_grid(AppState *state);
static void on_cell_clicked(GtkButton *button, AppState *state);
static void on_add_row_trigger_clicked(GtkButton *button, AppState *state);
static void on_add_row_confirm_clicked(GtkButton *button, AppState *state);
static void on_remove_row_confirm_clicked(GtkButton *button, AppState *state);
static void on_save_clicked(GtkButton *button, AppState *state);
static void on_delete_clicked(GtkButton *button, AppState *state);
static void on_css_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data);
static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);

// --- New animation functions ---
static gboolean start_expansion_animation(gpointer user_data);
static gboolean reveal_full_content(gpointer user_data);


// --- Helper Functions (Unchanged) ---
static void app_state_free(AppState *state) { if (state->css_monitor) { g_file_monitor_cancel(state->css_monitor); g_object_unref(state->css_monitor); } if (state->css_provider) g_object_unref(state->css_provider); if (state->schedule_data) json_node_free(state->schedule_data); if (state->time_slot_size_group) g_object_unref(state->time_slot_size_group); g_free(state->config_file_path); g_free(state); }
static AppState* app_state_new(GtkApplication *app) { AppState *state = g_new0(AppState, 1); state->app = app; state->main_window = GTK_WINDOW(gtk_application_window_new(app)); const char *config_dir = g_get_user_config_dir(); state->config_file_path = g_build_filename(config_dir, "gtk-schedule-app", "schedule.json", NULL); state->time_slot_size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL); g_signal_connect(state->main_window, "destroy", G_CALLBACK(app_state_free), state); return state; }
static void save_schedule_data(AppState *state) { JsonGenerator *generator = json_generator_new(); json_generator_set_root(generator, state->schedule_data); json_generator_set_pretty(generator, TRUE); char *dir = g_path_get_dirname(state->config_file_path); g_mkdir_with_parents(dir, 0755); g_free(dir); json_generator_to_file(generator, state->config_file_path, NULL); g_object_unref(generator); g_print("Schedule saved to %s\n", state->config_file_path); }
static int parse_time_slot_to_minutes(const char *slot_str) { int hour = 0, min = 0; char am_pm[3] = {0}; if (sscanf(slot_str, "%d:%d %2s", &hour, &min, am_pm) == 3) { if (strcmp(am_pm, "PM") == 0 && hour != 12) hour += 12; else if (strcmp(am_pm, "AM") == 0 && hour == 12) hour = 0; } else if (sscanf(slot_str, "%d:%d", &hour, &min) != 2) return -1; return (hour < 0 || hour > 23 || min < 0 || min > 59) ? -1 : hour * 60 + min; }
static gint sort_time_slots_compare_func(gconstpointer a, gconstpointer b) { const char *slot_a = json_node_get_string((JsonNode*)a); const char *slot_b = json_node_get_string((JsonNode*)b); return parse_time_slot_to_minutes(slot_a) - parse_time_slot_to_minutes(slot_b); }
static GtkStringList* create_time_list_store() { GtkStringList *string_list = gtk_string_list_new(NULL); for (int h = 0; h < 24; ++h) { for (int m = 0; m < 60; m += 30) { int display_h = (h == 0 || h == 12) ? 12 : h % 12; const char* am_pm = (h < 12) ? "AM" : "PM"; char *time_str = g_strdup_printf("%02d:%02d %s", display_h, m, am_pm); gtk_string_list_append(string_list, time_str); g_free(time_str); } } return string_list; }
static void on_add_row_trigger_clicked(GtkButton *button, AppState *state) { if (!state->settings_popover) { GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10); gtk_widget_set_margin_start(box, 10); gtk_widget_set_margin_end(box, 10); gtk_widget_set_margin_top(box, 10); gtk_widget_set_margin_bottom(box, 10); GtkStringList *time_model = create_time_list_store(); GtkWidget *start_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5); gtk_box_append(GTK_BOX(start_vbox), gtk_label_new("Start Time")); GtkWidget *start_dropdown = gtk_drop_down_new(G_LIST_MODEL(time_model), NULL); g_object_set_data(G_OBJECT(box), "start-dropdown", start_dropdown); gtk_box_append(GTK_BOX(start_vbox), start_dropdown); GtkWidget *end_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5); gtk_box_append(GTK_BOX(end_vbox), gtk_label_new("End Time")); GtkWidget *end_dropdown = gtk_drop_down_new(G_LIST_MODEL(time_model), NULL); g_object_set_data(G_OBJECT(box), "end-dropdown", end_dropdown); gtk_box_append(GTK_BOX(end_vbox), end_dropdown); GtkWidget *pickers_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); gtk_box_append(GTK_BOX(pickers_hbox), start_vbox); gtk_box_append(GTK_BOX(pickers_hbox), end_vbox); gtk_box_append(GTK_BOX(box), pickers_hbox); g_object_unref(time_model); GtkWidget *actions_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5); GtkWidget *remove_button = gtk_button_new(); g_object_set_data(G_OBJECT(box), "remove-button", remove_button); gtk_widget_add_css_class(remove_button, "destructive-action"); g_signal_connect(remove_button, "clicked", G_CALLBACK(on_remove_row_confirm_clicked), state); GtkWidget *add_button = gtk_button_new_with_label("Add"); gtk_widget_add_css_class(add_button, "suggested-action"); g_signal_connect(add_button, "clicked", G_CALLBACK(on_add_row_confirm_clicked), state); gtk_box_append(GTK_BOX(actions_hbox), remove_button); GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); gtk_widget_set_hexpand(spacer, TRUE); gtk_box_append(GTK_BOX(actions_hbox), spacer); gtk_box_append(GTK_BOX(actions_hbox), add_button); gtk_box_append(GTK_BOX(box), actions_hbox); state->settings_popover = gtk_popover_new(); gtk_popover_set_child(GTK_POPOVER(state->settings_popover), box); gtk_widget_set_parent(state->settings_popover, GTK_WIDGET(state->main_window)); } const char *time_slot_str = gtk_button_get_label(button); g_object_set_data(G_OBJECT(state->settings_popover), "active-time-slot", (gpointer)time_slot_str); GtkWidget *popover_box = gtk_popover_get_child(GTK_POPOVER(state->settings_popover)); GtkWidget *remove_button = GTK_WIDGET(g_object_get_data(G_OBJECT(popover_box), "remove-button")); char *remove_label = g_strdup_printf("Remove \"%s\"", time_slot_str); gtk_button_set_label(GTK_BUTTON(remove_button), remove_label); g_free(remove_label); JsonArray *time_slots_array = json_object_get_array_member(json_node_get_object(state->schedule_data), "time_slots"); gtk_widget_set_sensitive(remove_button, json_array_get_length(time_slots_array) > 1); double win_x, win_y; gtk_widget_translate_coordinates(GTK_WIDGET(button), GTK_WIDGET(state->main_window), 0, 0, &win_x, &win_y); GdkRectangle rect = { (int)win_x, (int)win_y, gtk_widget_get_width(GTK_WIDGET(button)), gtk_widget_get_height(GTK_WIDGET(button)) }; gtk_popover_set_pointing_to(GTK_POPOVER(state->settings_popover), &rect); gtk_popover_popup(GTK_POPOVER(state->settings_popover)); }
static void on_remove_row_confirm_clicked(GtkButton *button G_GNUC_UNUSED, AppState *state) { const char *slot_to_remove = g_object_get_data(G_OBJECT(state->settings_popover), "active-time-slot"); if (!slot_to_remove) return; JsonObject *root_obj = json_node_get_object(state->schedule_data); JsonArray *old_slots = json_object_get_array_member(root_obj, "time_slots"); JsonArray *new_slots = json_array_new(); for (guint i = 0; i < json_array_get_length(old_slots); i++) { const char *current_slot = json_array_get_string_element(old_slots, i); if (strcmp(current_slot, slot_to_remove) != 0) { json_array_add_string_element(new_slots, current_slot); } } json_object_set_array_member(root_obj, "time_slots", new_slots); json_object_remove_member(json_object_get_object_member(root_obj, "schedule"), slot_to_remove); build_schedule_grid(state); save_schedule_data(state); gtk_popover_popdown(GTK_POPOVER(state->settings_popover)); }
static void on_add_row_confirm_clicked(GtkButton *button G_GNUC_UNUSED, AppState *state) { GtkWidget *box = gtk_popover_get_child(GTK_POPOVER(state->settings_popover)); GtkDropDown *start_dropdown = GTK_DROP_DOWN(g_object_get_data(G_OBJECT(box), "start-dropdown")); GtkDropDown *end_dropdown = GTK_DROP_DOWN(g_object_get_data(G_OBJECT(box), "end-dropdown")); guint start_pos = gtk_drop_down_get_selected(start_dropdown); guint end_pos = gtk_drop_down_get_selected(end_dropdown); if (start_pos >= end_pos) { g_warning("Invalid time range selected."); return; } GtkStringList* model = GTK_STRING_LIST(gtk_drop_down_get_model(start_dropdown)); char *new_time_slot_str = g_strdup_printf("%s - %s", gtk_string_list_get_string(model, start_pos), gtk_string_list_get_string(model, end_pos)); JsonObject *root_obj = json_node_get_object(state->schedule_data); JsonArray *time_slots_array = json_object_get_array_member(root_obj, "time_slots"); json_array_add_string_element(time_slots_array, new_time_slot_str); GList *slots_list = json_array_get_elements(time_slots_array); slots_list = g_list_sort(slots_list, (GCompareFunc)sort_time_slots_compare_func); JsonArray *sorted_time_slots = json_array_new(); for (GList *l = slots_list; l != NULL; l = l->next) { json_array_add_element(sorted_time_slots, json_node_copy(l->data)); } json_object_set_array_member(root_obj, "time_slots", sorted_time_slots); g_list_free(slots_list); JsonArray *new_schedule_row = json_array_new(); for (int i = 0; i < 7; i++) json_array_add_null_element(new_schedule_row); json_object_set_array_member(json_object_get_object_member(root_obj, "schedule"), new_time_slot_str, new_schedule_row); build_schedule_grid(state); save_schedule_data(state); g_free(new_time_slot_str); gtk_popover_popdown(GTK_POPOVER(state->settings_popover)); }
static void on_save_clicked(GtkButton *button G_GNUC_UNUSED, AppState *state) { GtkButton *active_button = GTK_BUTTON(g_object_get_data(G_OBJECT(state->edit_popover), "active-button")); int row = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(active_button), "row")); int col = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(active_button), "col")); GtkWidget *box = gtk_popover_get_child(GTK_POPOVER(state->edit_popover)); GtkEditable *title_entry = GTK_EDITABLE(g_object_get_data(G_OBJECT(box), "title-entry")); GtkTextBuffer *desc_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(box), "desc-view"))); const char *new_title = gtk_editable_get_text(title_entry); GtkTextIter start, end; gtk_text_buffer_get_bounds(desc_buffer, &start, &end); gchar *new_desc = gtk_text_buffer_get_text(desc_buffer, &start, &end, FALSE); JsonObject *root_obj = json_node_get_object(state->schedule_data); JsonArray *time_slots = json_object_get_array_member(root_obj, "time_slots"); JsonObject *schedule = json_object_get_object_member(root_obj, "schedule"); const char *time_slot_str = json_array_get_string_element(time_slots, row); JsonArray *day_events = json_object_get_array_member(schedule, time_slot_str); JsonObject *new_event_obj = json_object_new(); json_object_set_string_member(new_event_obj, "title", new_title); json_object_set_string_member(new_event_obj, "description", new_desc); JsonNode *new_event_node = json_node_new(JSON_NODE_OBJECT); json_node_take_object(new_event_node, new_event_obj); JsonArray *new_day_events = json_array_new(); for (guint i = 0; i < json_array_get_length(day_events); i++) { if (i == (guint)col) json_array_add_element(new_day_events, json_node_copy(new_event_node)); else json_array_add_element(new_day_events, json_node_copy(json_array_get_element(day_events, i))); } json_object_set_array_member(schedule, time_slot_str, new_day_events); gtk_button_set_label(active_button, new_title); gtk_widget_add_css_class(GTK_WIDGET(active_button), "event-filled"); save_schedule_data(state); g_free(new_desc); json_node_free(new_event_node); gtk_popover_popdown(GTK_POPOVER(state->edit_popover)); }
static void on_delete_clicked(GtkButton *button G_GNUC_UNUSED, AppState *state) { GtkButton *active_button = GTK_BUTTON(g_object_get_data(G_OBJECT(state->edit_popover), "active-button")); int row = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(active_button), "row")); int col = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(active_button), "col")); JsonObject *root_obj = json_node_get_object(state->schedule_data); JsonArray *time_slots = json_object_get_array_member(root_obj, "time_slots"); JsonObject *schedule = json_object_get_object_member(root_obj, "schedule"); const char *time_slot_str = json_array_get_string_element(time_slots, row); JsonArray *day_events = json_object_get_array_member(schedule, time_slot_str); JsonArray *new_day_events = json_array_new(); for (guint i = 0; i < json_array_get_length(day_events); i++) { if (i == (guint)col) json_array_add_null_element(new_day_events); else json_array_add_element(new_day_events, json_node_copy(json_array_get_element(day_events, i))); } json_object_set_array_member(schedule, time_slot_str, new_day_events); gtk_button_set_label(active_button, ""); gtk_widget_remove_css_class(GTK_WIDGET(active_button), "event-filled"); save_schedule_data(state); gtk_popover_popdown(GTK_POPOVER(state->edit_popover)); }
static void on_cell_clicked(GtkButton *button, AppState *state) { if (!state->edit_popover) { GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10); gtk_widget_set_margin_start(box, 10); gtk_widget_set_margin_end(box, 10); gtk_widget_set_margin_top(box, 10); gtk_widget_set_margin_bottom(box, 10); GtkWidget *title_entry = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(title_entry), "Event Title"); g_object_set_data(G_OBJECT(box), "title-entry", title_entry); gtk_box_append(GTK_BOX(box), title_entry); GtkWidget *scrolled_window = gtk_scrolled_window_new(); gtk_widget_set_size_request(scrolled_window, 250, 100); GtkWidget *desc_view = gtk_text_view_new(); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(desc_view), GTK_WRAP_WORD_CHAR); g_object_set_data(G_OBJECT(box), "desc-view", desc_view); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), desc_view); gtk_box_append(GTK_BOX(box), scrolled_window); GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5); GtkWidget *delete_button = gtk_button_new_with_label("Delete"); gtk_widget_add_css_class(delete_button, "destructive-action"); g_signal_connect(delete_button, "clicked", G_CALLBACK(on_delete_clicked), state); GtkWidget *save_button = gtk_button_new_with_label("Save"); gtk_widget_add_css_class(save_button, "suggested-action"); g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_clicked), state); GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); gtk_widget_set_hexpand(spacer, TRUE); gtk_box_append(GTK_BOX(button_box), delete_button); gtk_box_append(GTK_BOX(button_box), spacer); gtk_box_append(GTK_BOX(button_box), save_button); gtk_box_append(GTK_BOX(box), button_box); state->edit_popover = gtk_popover_new(); gtk_popover_set_child(GTK_POPOVER(state->edit_popover), box); gtk_widget_set_parent(state->edit_popover, GTK_WIDGET(state->main_window)); } g_object_set_data(G_OBJECT(state->edit_popover), "active-button", button); int row = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "row")); int col = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "col")); JsonObject *root_obj = json_node_get_object(state->schedule_data); JsonArray *time_slots = json_object_get_array_member(root_obj, "time_slots"); const char *time_slot_str = json_array_get_string_element(time_slots, row); JsonArray *day_events = json_object_get_array_member(json_object_get_object_member(root_obj, "schedule"), time_slot_str); JsonNode *event_node = json_array_get_element(day_events, col); GtkWidget *box = gtk_popover_get_child(GTK_POPOVER(state->edit_popover)); GtkEditable *title_entry = GTK_EDITABLE(g_object_get_data(G_OBJECT(box), "title-entry")); GtkTextBuffer *desc_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(box), "desc-view"))); if (JSON_NODE_HOLDS_OBJECT(event_node)) { JsonObject *event_obj = json_node_get_object(event_node); gtk_editable_set_text(title_entry, json_object_get_string_member(event_obj, "title")); gtk_text_buffer_set_text(desc_buffer, json_object_get_string_member(event_obj, "description"), -1); } else { gtk_editable_set_text(title_entry, ""); gtk_text_buffer_set_text(desc_buffer, "", -1); } double win_x, win_y; gtk_widget_translate_coordinates(GTK_WIDGET(button), GTK_WIDGET(state->main_window), 0, 0, &win_x, &win_y); GdkRectangle rect = { (int)win_x, (int)win_y, gtk_widget_get_width(GTK_WIDGET(button)), gtk_widget_get_height(GTK_WIDGET(button)) }; gtk_popover_set_pointing_to(GTK_POPOVER(state->edit_popover), &rect); gtk_popover_popup(GTK_POPOVER(state->edit_popover)); }
int main(int argc, char **argv) { const char *app_id = "com.meismeric.GtkSchedule"; GtkApplication *app = gtk_application_new(app_id, G_APPLICATION_DEFAULT_FLAGS); g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL); int status = g_application_run(G_APPLICATION(app), argc, argv); g_object_unref(app); return status; }
static void load_or_create_schedule_data(AppState *state) { JsonParser *parser = json_parser_new(); GError *error = NULL; if (g_file_test(state->config_file_path, G_FILE_TEST_EXISTS)) { json_parser_load_from_file(parser, state->config_file_path, &error); } else { const char *resource_path = "/com/meismeric/GtkSchedule/schedule.json"; GBytes *bytes = g_resources_lookup_data(resource_path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL); if (bytes) { json_parser_load_from_data(parser, g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes), &error); g_bytes_unref(bytes); } } if (error) { g_error_free(error); g_object_unref(parser); state->schedule_data = NULL; return; } state->schedule_data = json_node_copy(json_parser_get_root(parser)); g_object_unref(parser); save_schedule_data(state); }
static void on_css_changed(GFileMonitor *monitor G_GNUC_UNUSED, GFile *file, GFile *other_file G_GNUC_UNUSED, GFileMonitorEvent event_type, gpointer user_data) { if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) { AppState *state = user_data; char *path = g_file_get_path(file); g_print("CSS file changed, reloading from: %s\n", path); gtk_css_provider_load_from_path(state->css_provider, path); g_free(path); } }
static gboolean on_key_pressed(GtkEventControllerKey *controller G_GNUC_UNUSED, guint keyval, guint keycode G_GNUC_UNUSED, GdkModifierType state G_GNUC_UNUSED, gpointer user_data) { AppState *app_state = user_data; if (keyval == GDK_KEY_Escape) { g_application_quit(G_APPLICATION(app_state->app)); return TRUE; } return FALSE; }

static gboolean start_expansion_animation(gpointer user_data) {
    AppState *state = user_data;
    gtk_widget_add_css_class(state->main_container, "expanded");
    g_timeout_add(100, reveal_full_content, state); // Start revealing content shortly after expansion starts
    return G_SOURCE_REMOVE;
}

// --- CORRECTED: The logic for swapping the pill for the full content ---
static gboolean reveal_full_content(gpointer user_data) {
    AppState *state = user_data;

    // THE CRITICAL FIX: Add the full content revealer to the container FIRST
    gtk_box_append(GTK_BOX(state->main_container), state->content_revealer);

    // Then, remove the initial "Schedule" pill label
    gtk_box_remove(GTK_BOX(state->main_container), state->pill_label);
    
    // Now, tell the revealer to show the full schedule content
    gtk_revealer_set_reveal_child(GTK_REVEALER(state->content_revealer), TRUE);

    return G_SOURCE_REMOVE;
}


static void on_activate(GtkApplication *app) {
    AppState *state = app_state_new(app);
    load_or_create_schedule_data(state);
    if (!state->schedule_data) { g_application_quit(G_APPLICATION(app)); app_state_free(state); return; }
    
    // --- YOUR original, correct CSS loading logic is fully restored ---
    state->css_provider = gtk_css_provider_new();
    if (g_file_test("data/style.css", G_FILE_TEST_EXISTS)) {
        gtk_css_provider_load_from_path(state->css_provider, "data/style.css");
    } else {
        g_print("Could not load data/style.css, falling back to resource.\n");
        gtk_css_provider_load_from_resource(state->css_provider, "/com/meismeric/GtkSchedule/style.css");
    }
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(state->css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    GFile *css_file = g_file_new_for_path("data/style.css");
    state->css_monitor = g_file_monitor_file(css_file, G_FILE_MONITOR_NONE, NULL, NULL);
    g_signal_connect(state->css_monitor, "changed", G_CALLBACK(on_css_changed), state);
    g_object_unref(css_file);
    // --- End of your CSS logic ---

    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), state);
    gtk_widget_add_controller(GTK_WIDGET(state->main_window), key_controller);

    build_ui(state);
    gtk_window_present(state->main_window);

    // The timer that triggers the animation remains the same
    g_timeout_add(300, start_expansion_animation, state);
}
// build_schedule_grid is now simpler: it just builds and returns the grid
static GtkWidget* build_schedule_grid_widget(AppState *state) {
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_widget_set_margin_start(grid, 10);
    gtk_widget_set_margin_end(grid, 10);
    gtk_widget_set_margin_top(grid, 10);
    gtk_widget_set_margin_bottom(grid, 10);

    JsonObject *root_obj = json_node_get_object(state->schedule_data);
    JsonArray *time_slots_array = json_object_get_array_member(root_obj, "time_slots");
    JsonObject *schedule_obj = json_object_get_object_member(root_obj, "schedule");
    const char *days[] = {"", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    for (int i = 1; i < 8; i++) {
        GtkWidget *day_label = gtk_label_new(days[i]);
        gtk_widget_add_css_class(day_label, "header");
        gtk_grid_attach(GTK_GRID(grid), day_label, i, 0, 1, 1);
    }
    for (guint i = 0; i < json_array_get_length(time_slots_array); i++) {
        const char *time_slot_str = json_array_get_string_element(time_slots_array, i);
        int current_row = i + 1;
        GtkWidget *time_button = gtk_button_new_with_label(time_slot_str);
        gtk_size_group_add_widget(state->time_slot_size_group, time_button);
        gtk_widget_set_halign(time_button, GTK_ALIGN_END);
        gtk_widget_add_css_class(time_button, "header");
        gtk_button_set_has_frame(GTK_BUTTON(time_button), FALSE);
        g_signal_connect(time_button, "clicked", G_CALLBACK(on_add_row_trigger_clicked), state);
        gtk_grid_attach(GTK_GRID(grid), time_button, 0, current_row, 1, 1);
        JsonArray *day_events_array = json_object_get_array_member(schedule_obj, time_slot_str);
        for (guint j = 0; j < 7; j++) {
            JsonNode *event_node = day_events_array ? json_array_get_element(day_events_array, j) : NULL;
            GtkWidget *button;
            if (event_node && JSON_NODE_HOLDS_OBJECT(event_node)) {
                const char *title = json_object_get_string_member(json_node_get_object(event_node), "title");
                button = gtk_button_new_with_label(title);
                gtk_widget_add_css_class(button, "event-filled");
            } else {
                button = gtk_button_new_with_label("");
            }
            g_object_set_data(G_OBJECT(button), "row", GINT_TO_POINTER(i));
            g_object_set_data(G_OBJECT(button), "col", GINT_TO_POINTER(j));
            g_signal_connect(button, "clicked", G_CALLBACK(on_cell_clicked), state);
            gtk_grid_attach(GTK_GRID(grid), button, j + 1, current_row, 1, 1);
        }
    }
    return grid;
}

// Overhauled build_schedule_grid that manages adding/removing the widget
static void build_schedule_grid(AppState *state) {
    GtkWidget *full_content_box = gtk_revealer_get_child(GTK_REVEALER(state->content_revealer));
    if (state->schedule_grid) {
        gtk_box_remove(GTK_BOX(full_content_box), state->schedule_grid);
    }
    state->schedule_grid = build_schedule_grid_widget(state);
    gtk_box_append(GTK_BOX(full_content_box), state->schedule_grid);
}

static void build_ui(AppState *state) {
    gtk_window_set_title(state->main_window, "Schedule Widget");
    gtk_layer_init_for_window(state->main_window);
    gtk_layer_set_layer(state->main_window, GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_keyboard_mode(state->main_window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    gtk_layer_set_anchor(state->main_window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_margin(state->main_window, GTK_LAYER_SHELL_EDGE_TOP, 10);

    // 1. Create the main container
    state->main_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // --- THE FIX: Set the widget name so your CSS ID selector (#main-container) works ---
    gtk_widget_set_name(state->main_container, "main-container");
    // --- END OF FIX ---

    gtk_widget_set_halign(state->main_container, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(state->main_container, GTK_ALIGN_START);

    // 2. Create the initial "Schedule" pill
    state->pill_label = gtk_label_new("󰄱  Schedule");
    gtk_widget_add_css_class(state->pill_label, "pill-label"); // This class is fine
    gtk_widget_set_vexpand(state->pill_label, TRUE);
    gtk_widget_set_valign(state->pill_label, GTK_ALIGN_CENTER);

    // 3. Create the box that will hold the full UI (header + grid)
    GtkWidget *full_content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(header_box, 5);
    GtkWidget *title = gtk_label_new("<b>Schedule</b>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(header_box), title);
    gtk_box_append(GTK_BOX(full_content_box), header_box);

    // Build the grid and add it to this box
    state->schedule_grid = build_schedule_grid_widget(state);
    gtk_box_append(GTK_BOX(full_content_box), state->schedule_grid);

    // 4. Wrap the full UI in a revealer
    state->content_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(state->content_revealer), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    gtk_revealer_set_transition_duration(GTK_REVEALER(state->content_revealer), 400);
    gtk_revealer_set_child(GTK_REVEALER(state->content_revealer), full_content_box);

    // 5. Initially, add ONLY the pill label to the main container.
    gtk_box_append(GTK_BOX(state->main_container), state->pill_label);

    gtk_window_set_child(state->main_window, state->main_container);
}