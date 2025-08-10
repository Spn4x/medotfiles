#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <time.h>
#include <json-glib/json-glib.h>
#include <math.h>

#define APP_WIDTH 550

// -- DATA STRUCTURES --
typedef struct {
    gchar *time;
    gchar *title;
} Event;

typedef struct {
    GDateTime *datetime;
    Event *event;
} UpcomingEvent;

typedef struct {
    int day_to_add;
    int days_in_month;
    int grid_x;
    int grid_y;
    int current_y;
    int current_m;
    int today_y;
    int today_m;
    int today_d;
} GridPopulationState;

typedef struct {
    GtkWindow *main_window;
    GtkLabel *month_label;
    GtkGrid *calendar_grid;
    GtkListBox *upcoming_list_box;
    GDateTime *current_date;
    GHashTable *events;
    GHashTable *permanent_events;
    GtkCssProvider *css_provider;
    GtkPopover *add_event_popover;
    GtkEntry *add_event_title_entry;
    GtkCheckButton *add_event_allday_check;
    GtkDropDown *add_event_hour_dropdown;
    GtkDropDown *add_event_minute_dropdown;
    GtkDropDown *add_event_ampm_dropdown;
    GtkWidget *add_event_time_box;
    guint grid_population_timer_id;
} CalendarApp;

typedef struct {
    CalendarApp *app;
    gchar *date_key;
    Event *event_to_delete;
    GtkPopover *popover;
} DeleteEventData;

typedef struct {
    GtkWindow *main_window;
    gint64 start_time;
    gint duration;
    gdouble start_margin;
    gdouble end_margin;
} AnimationState;

typedef struct {
    GHashTable *events;
    GHashTable *permanent_events;
} LoadedData;


// -- FORWARD DECLARATIONS --
static void start_grid_population(CalendarApp *app);
static void populate_upcoming_events_list(CalendarApp *app);


// -- MEMORY MANAGEMENT --
static void free_event(gpointer data) { Event *event = (Event *)data; g_free(event->time); g_free(event->title); g_free(event); }
static void free_event_list(gpointer list_data) { g_list_free_full((GList *)list_data, free_event); }
static void free_upcoming_event(gpointer data) { UpcomingEvent *ue = (UpcomingEvent *)data; g_date_time_unref(ue->datetime); g_free(ue); }
static void free_delete_event_data(gpointer data) {
    DeleteEventData *ded = (DeleteEventData *)data;
    g_free(ded->date_key);
    g_free(ded);
}

// -- HELPER FUNCTIONS --
static gchar* format_time_to_12h(const gchar *time_24h) {
    if (g_strcmp0(time_24h, "all-day") == 0) return g_strdup("All-day");
    int hour, minute;
    if (sscanf(time_24h, "%d:%d", &hour, &minute) == 2) {
        const char *am_pm = (hour < 12) ? "AM" : "PM";
        if (hour == 0) hour = 12;
        else if (hour > 12) hour -= 12;
        return g_strdup_printf("%d:%02d %s", hour, minute, am_pm);
    }
    return g_strdup(time_24h);
}


// -- EVENT DATA HANDLING --
static GHashTable* load_event_file(const char *path) {
    GHashTable *events_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_event_list);
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_file(parser, path, NULL)) {
        g_object_unref(parser);
        return events_table;
    }
    JsonNode *root = json_parser_get_root(parser);
    JsonObject *root_obj = json_node_get_object(root);
    JsonObjectIter iter;
    json_object_iter_init(&iter, root_obj);
    const gchar *date_key;
    JsonNode *member_node;
    while (json_object_iter_next(&iter, &date_key, &member_node)) {
        GList *event_list = NULL;
        JsonArray *events_array = json_node_get_array(member_node);
        for (guint i = 0; i < json_array_get_length(events_array); i++) {
            JsonObject *event_obj = json_array_get_object_element(events_array, i);
            Event *event = g_new(Event, 1);
            event->time = g_strdup(json_object_get_string_member(event_obj, "time"));
            event->title = g_strdup(json_object_get_string_member(event_obj, "title"));
            event_list = g_list_prepend(event_list, event);
        }
        event_list = g_list_reverse(event_list);
        g_hash_table_insert(events_table, g_strdup(date_key), event_list);
    }
    g_object_unref(parser);
    return events_table;
}

static void load_data_in_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable) {
    (void)source_object; (void)task_data; (void)cancellable;
    LoadedData *loaded_data = g_new(LoadedData, 1);
    loaded_data->events = load_event_file("data/events.json");
    loaded_data->permanent_events = load_event_file("data/permanent_events.json");
    g_task_return_pointer(task, loaded_data, g_free);
}

static void save_events(CalendarApp *app) {
    JsonObject *root_obj = json_object_new();
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, app->events);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        JsonArray *json_event_array = json_array_new();
        for (GList *l = (GList *)value; l != NULL; l = l->next) {
            Event *event = (Event *)l->data;
            JsonObject *json_event = json_object_new();
            json_object_set_string_member(json_event, "time", event->time);
            json_object_set_string_member(json_event, "title", event->title);
            json_array_add_object_element(json_event_array, json_event);
        }
        json_object_set_array_member(root_obj, (gchar *)key, json_event_array);
    }
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_node_init_object(json_node_alloc(), root_obj));
    json_generator_set_pretty(gen, TRUE);
    json_generator_to_file(gen, "data/events.json", NULL);
    g_object_unref(gen);
}


// -- UI CALLBACKS --
// --- FINAL CRASH FIX: This function uses the safe "steal, modify, replace" pattern ---
static void on_delete_event_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    DeleteEventData *data = (DeleteEventData *)user_data;
    CalendarApp *app = data->app;

    gpointer original_key = NULL;
    gpointer original_list_ptr = NULL;

    // 1. STEAL: Atomically remove the entry. We now own the key and the list.
    // The hash table's automatic cleanup will NOT run for this entry.
    gboolean found = g_hash_table_steal_extended(
        app->events,
        data->date_key,
        &original_key,
        &original_list_ptr
    );

    if (!found) { return; }

    GList *event_list = (GList *)original_list_ptr;

    // 2. MODIFY: Now that we have exclusive ownership, we can safely modify the list.
    GList *link_to_delete = g_list_find(event_list, data->event_to_delete);
    if (link_to_delete) {
        GList *new_list_head = g_list_delete_link(event_list, link_to_delete);
        
        // We are now responsible for freeing the unlinked event's memory.
        free_event(data->event_to_delete);
        
        // 3. REPLACE or DISCARD
        if (new_list_head) {
            // The list is not empty, so we give ownership of the key and the modified list BACK to the hash table.
            g_hash_table_insert(app->events, original_key, new_list_head);
        } else {
            // The list is now empty, so there's nothing to put back.
            // We are responsible for freeing the key we stole. The empty list is already freed by g_list_delete_link.
            g_free(original_key);
        }
    } else {
        // Failsafe: if the event wasn't found (should be impossible), put the original list and key back.
        g_hash_table_insert(app->events, original_key, event_list);
    }

    save_events(app);
    start_grid_population(app);
    populate_upcoming_events_list(app);
    gtk_popover_popdown(data->popover);
}

static void on_add_event_save(GtkButton *button, gpointer user_data) {
    (void)button;
    CalendarApp *app = (CalendarApp *)user_data;
    const gchar *date_key = g_object_get_data(G_OBJECT(app->add_event_popover), "date-key");
    const char *title_text = gtk_editable_get_text(GTK_EDITABLE(app->add_event_title_entry));
    if (strlen(title_text) == 0) return;
    gchar *time_str;
    if (gtk_check_button_get_active(app->add_event_allday_check)) {
        time_str = g_strdup("all-day");
    } else {
        guint hour_12 = gtk_drop_down_get_selected(app->add_event_hour_dropdown) + 1;
        guint minute = gtk_drop_down_get_selected(app->add_event_minute_dropdown) * 5;
        guint is_pm = gtk_drop_down_get_selected(app->add_event_ampm_dropdown);
        guint hour_24 = hour_12;
        if (is_pm && hour_12 != 12) hour_24 += 12;
        else if (!is_pm && hour_12 == 12) hour_24 = 0;
        time_str = g_strdup_printf("%02d:%02d", hour_24, minute);
    }
    Event *new_event = g_new(Event, 1);
    new_event->time = time_str;
    new_event->title = g_strdup(title_text);
    GList *event_list = g_hash_table_lookup(app->events, date_key);
    if (event_list == NULL) {
        event_list = g_list_append(NULL, new_event);
        g_hash_table_insert(app->events, g_strdup(date_key), event_list);
    } else {
        event_list = g_list_append(event_list, new_event);
        g_hash_table_replace(app->events, g_strdup(date_key), event_list);
    }
    save_events(app);
    start_grid_population(app);
    populate_upcoming_events_list(app);
}

static void on_allday_toggled(GtkCheckButton *checkbutton, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    CalendarApp *app = (CalendarApp *)user_data;
    gtk_widget_set_sensitive(app->add_event_time_box, !gtk_check_button_get_active(checkbutton));
}

static void on_css_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {
    (void)monitor; (void)file; (void)other_file;
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
        CalendarApp *app = (CalendarApp *)user_data;
        gtk_css_provider_load_from_path(app->css_provider, "src/style.css");
    }
}

static void on_day_left_clicked(GtkButton *button, gpointer user_data) {
    CalendarApp *app = (CalendarApp *)user_data;
    const gchar *date_key_full = g_object_get_data(G_OBJECT(button), "date-key");
    gchar *date_key_permanent = g_strdup_printf("%d-%d", g_date_time_get_month(app->current_date), atoi(gtk_button_get_label(button)));

    GList *regular_events = g_hash_table_lookup(app->events, date_key_full);
    GList *permanent_events = g_hash_table_lookup(app->permanent_events, date_key_permanent);
    g_free(date_key_permanent);

    if (!regular_events && !permanent_events) return;

    GtkWidget *popover = gtk_popover_new();
    gtk_widget_add_css_class(popover, "event-popover");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_popover_set_child(GTK_POPOVER(popover), vbox);

    for (GList *l = permanent_events; l != NULL; l = l->next) {
        Event *event = (Event *)l->data;
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(hbox, "event-entry");
        GtkWidget *icon = gtk_image_new_from_icon_name("starred-symbolic");
        gtk_widget_add_css_class(icon, "permanent-event-icon");
        gchar *time_12h = format_time_to_12h(event->time);
        GtkWidget *time_label = gtk_label_new(time_12h); g_free(time_12h);
        gtk_widget_add_css_class(time_label, "event-time");
        GtkWidget *title_label = gtk_label_new(event->title);
        gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
        gtk_widget_add_css_class(title_label, "event-title");
        gtk_box_append(GTK_BOX(hbox), icon);
        gtk_box_append(GTK_BOX(hbox), time_label);
        gtk_box_append(GTK_BOX(hbox), title_label);
        gtk_box_append(GTK_BOX(vbox), hbox);
    }
    
    for (GList *l = regular_events; l != NULL; l = l->next) {
        Event *event = (Event *)l->data;
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(hbox, "event-entry");
        gchar *time_12h = format_time_to_12h(event->time);
        GtkWidget *time_label = gtk_label_new(time_12h); g_free(time_12h);
        gtk_widget_add_css_class(time_label, "event-time");
        GtkWidget *title_label = gtk_label_new(event->title);
        gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
        gtk_widget_add_css_class(title_label, "event-title");
        gtk_widget_set_hexpand(title_label, TRUE);
        GtkWidget *delete_button = gtk_button_new_from_icon_name("edit-delete-symbolic");
        gtk_widget_add_css_class(delete_button, "delete-button");
        DeleteEventData *ded = g_new(DeleteEventData, 1);
        ded->app = app;
        ded->date_key = g_strdup(date_key_full);
        ded->event_to_delete = event;
        ded->popover = GTK_POPOVER(popover);
        g_signal_connect(delete_button, "clicked", G_CALLBACK(on_delete_event_clicked), ded);
        g_object_set_data_full(G_OBJECT(delete_button), "delete-data", ded, free_delete_event_data);
        gtk_box_append(GTK_BOX(hbox), time_label);
        gtk_box_append(GTK_BOX(hbox), title_label);
        gtk_box_append(GTK_BOX(hbox), delete_button);
        gtk_box_append(GTK_BOX(vbox), hbox);
    }

    gtk_widget_set_parent(popover, GTK_WIDGET(button));
    g_signal_connect_swapped(popover, "closed", G_CALLBACK(gtk_widget_unparent), popover);
    gtk_popover_popup(GTK_POPOVER(popover));
    g_object_unref(popover);
}

static void on_day_right_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    (void)n_press; (void)x; (void)y;
    CalendarApp *app = (CalendarApp *)user_data;
    GtkWidget *button = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    const gchar *date_key = g_object_get_data(G_OBJECT(button), "date-key");
    g_object_set_data(G_OBJECT(app->add_event_popover), "date-key", (gpointer)date_key);
    gtk_editable_set_text(GTK_EDITABLE(app->add_event_title_entry), "");
    gtk_check_button_set_active(app->add_event_allday_check, FALSE);
    gtk_drop_down_set_selected(app->add_event_hour_dropdown, 8);
    gtk_drop_down_set_selected(app->add_event_minute_dropdown, 0);
    gtk_drop_down_set_selected(app->add_event_ampm_dropdown, 0);
    gtk_widget_set_parent(GTK_WIDGET(app->add_event_popover), button);
    gtk_popover_popup(GTK_POPOVER(app->add_event_popover));
}

static gboolean on_animation_tick(gpointer user_data) {
    AnimationState *state = (AnimationState *)user_data;
    gint64 now = g_get_monotonic_time();
    double progress = (double)(now - state->start_time) / state->duration;
    if (progress >= 1.0) {
        gtk_layer_set_margin(state->main_window, GTK_LAYER_SHELL_EDGE_TOP, state->end_margin);
        g_free(state);
        return G_SOURCE_REMOVE;
    }
    double eased_progress = sin(progress * G_PI_2);
    double current_margin = state->start_margin + (state->end_margin - state->start_margin) * eased_progress;
    gtk_layer_set_margin(state->main_window, GTK_LAYER_SHELL_EDGE_TOP, current_margin);
    return G_SOURCE_CONTINUE;
}

static void start_slide_in_animation(GtkWidget *widget, gpointer user_data) {
    (void)user_data;
    AnimationState *state = g_new(AnimationState, 1);
    state->main_window = GTK_WINDOW(widget);
    int height = gtk_widget_get_height(widget);
    state->start_time = g_get_monotonic_time();
    state->duration = 350000;
    state->start_margin = -(height + 10);
    state->end_margin = 10;
    gtk_layer_set_margin(state->main_window, GTK_LAYER_SHELL_EDGE_TOP, state->start_margin);
    g_timeout_add(16, on_animation_tick, state);
    g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(start_slide_in_animation), user_data);
}

static void on_data_loaded(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    (void)source_object;
    CalendarApp *app = (CalendarApp *)user_data;
    GError *error = NULL;
    GTask *task = G_TASK(res);
    LoadedData *loaded_data = g_task_propagate_pointer(task, &error);
    if (error) {
        g_warning("Failed to load event data: %s", error->message);
        g_error_free(error);
        if (loaded_data) {
            g_hash_table_unref(loaded_data->events);
            g_hash_table_unref(loaded_data->permanent_events);
            g_free(loaded_data);
        }
        return;
    }

    if (app->events) g_hash_table_unref(app->events);
    if (app->permanent_events) g_hash_table_unref(app->permanent_events);
    app->events = loaded_data->events;
    app->permanent_events = loaded_data->permanent_events;
    g_free(loaded_data);

    start_grid_population(app);
    populate_upcoming_events_list(app);
}


// -- CORE UI LOGIC --
static gboolean populate_one_day(gpointer user_data) {
    CalendarApp *app = (CalendarApp*)user_data;
    GridPopulationState *state = g_object_get_data(G_OBJECT(app->calendar_grid), "population-state");

    if (!state || state->day_to_add > state->days_in_month) {
        if (state) {
            g_free(state);
            g_object_set_data(G_OBJECT(app->calendar_grid), "population-state", NULL);
        }
        app->grid_population_timer_id = 0;
        return G_SOURCE_REMOVE;
    }
    gchar label_str[4];
    snprintf(label_str, 4, "%d", state->day_to_add);
    GtkWidget *button = gtk_button_new_with_label(label_str);
    gtk_widget_add_css_class(button, "day-button");
    gtk_widget_add_css_class(button, "fade-in");
    
    gchar *date_key = g_strdup_printf("%d-%d-%d", state->current_y, state->current_m, state->day_to_add);
    gchar *permanent_key = g_strdup_printf("%d-%d", state->current_m, state->day_to_add);

    g_object_set_data_full(G_OBJECT(button), "date-key", date_key, (GDestroyNotify)g_free);
    if (state->day_to_add == state->today_d && state->current_m == state->today_m && state->current_y == state->today_y) {
        gtk_widget_add_css_class(button, "today");
    }
    if ((app->events && g_hash_table_contains(app->events, date_key)) || (app->permanent_events && g_hash_table_contains(app->permanent_events, permanent_key))) {
        gtk_widget_add_css_class(button, "has-event");
    }
    g_free(permanent_key);
    
    g_signal_connect(button, "clicked", G_CALLBACK(on_day_left_clicked), app);
    GtkGesture *right_click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click_gesture), GDK_BUTTON_SECONDARY);
    g_signal_connect(right_click_gesture, "pressed", G_CALLBACK(on_day_right_clicked), app);
    gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(right_click_gesture));
    gtk_grid_attach(app->calendar_grid, button, state->grid_x, state->grid_y, 1, 1);
    state->grid_x++;
    if (state->grid_x > 6) { state->grid_x = 0; state->grid_y++; }
    state->day_to_add++;
    return G_SOURCE_CONTINUE;
}

static void start_grid_population(CalendarApp *app) {
    if (gtk_widget_get_parent(GTK_WIDGET(app->add_event_popover))) {
        gtk_popover_popdown(app->add_event_popover);
    }

    if (app->grid_population_timer_id > 0) {
        g_source_remove(app->grid_population_timer_id);
        GridPopulationState *old_state = g_object_get_data(G_OBJECT(app->calendar_grid), "population-state");
        if (old_state) {
            g_free(old_state);
            g_object_set_data(G_OBJECT(app->calendar_grid), "population-state", NULL);
        }
    }
    
    gchar *month_str = g_date_time_format(app->current_date, "%B %Y");
    gtk_label_set_text(app->month_label, month_str);
    g_free(month_str);
    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(app->calendar_grid));
    while(child) {
        GtkWidget *next_child = gtk_widget_get_next_sibling(child);
        if (gtk_widget_has_css_class(child, "day-button")) gtk_grid_remove(GTK_GRID(app->calendar_grid), child);
        child = next_child;
    }
    GridPopulationState *state = g_new0(GridPopulationState, 1);
    state->day_to_add = 1;
    state->current_y = g_date_time_get_year(app->current_date);
    state->current_m = g_date_time_get_month(app->current_date);
    state->days_in_month = g_date_get_days_in_month((GDateMonth)state->current_m, (GDateYear)state->current_y);
    GDateTime *first_day_of_month = g_date_time_new(g_date_time_get_timezone(app->current_date), state->current_y, state->current_m, 1, 0, 0, 0);
    state->grid_x = g_date_time_get_day_of_week(first_day_of_month) % 7;
    state->grid_y = 1;
    g_date_time_unref(first_day_of_month);
    GDateTime *today = g_date_time_new_now_local();
    state->today_y = g_date_time_get_year(today);
    state->today_m = g_date_time_get_month(today);
    state->today_d = g_date_time_get_day_of_month(today);
    g_date_time_unref(today);
    
    g_object_set_data(G_OBJECT(app->calendar_grid), "population-state", state);
    app->grid_population_timer_id = g_timeout_add(5, populate_one_day, app);
}

static void on_prev_month_clicked(GtkButton *button, gpointer user_data) { (void)button; CalendarApp *app = (CalendarApp *)user_data; app->current_date = g_date_time_add_months(app->current_date, -1); start_grid_population(app); }
static void on_next_month_clicked(GtkButton *button, gpointer user_data) { (void)button; CalendarApp *app = (CalendarApp *)user_data; app->current_date = g_date_time_add_months(app->current_date, 1); start_grid_population(app); }

static GtkWidget* create_upcoming_event_row(UpcomingEvent *ue) {
    GtkWidget *row = gtk_list_box_row_new(); gtk_widget_add_css_class(row, "upcoming-row");
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), hbox);
    GtkWidget *date_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_widget_set_valign(date_box, GTK_ALIGN_CENTER); gtk_widget_add_css_class(date_box, "upcoming-date-box");
    gchar *day_str = g_date_time_format(ue->datetime, "%d"); gchar *month_str = g_date_time_format(ue->datetime, "%b");
    GtkWidget *day_label = gtk_label_new(day_str); gtk_widget_add_css_class(day_label, "upcoming-date-day");
    GtkWidget *month_label = gtk_label_new(month_str); gtk_widget_add_css_class(month_label, "upcoming-date-month");
    gtk_box_append(GTK_BOX(date_box), day_label); gtk_box_append(GTK_BOX(date_box), month_label); g_free(day_str); g_free(month_str);
    GtkWidget *details_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *title_label = gtk_label_new(ue->event->title); gtk_label_set_xalign(GTK_LABEL(title_label), 0.0); gtk_widget_add_css_class(title_label, "upcoming-event-title");
    gchar *time_12h = format_time_to_12h(ue->event->time);
    GtkWidget *time_label = gtk_label_new(time_12h); g_free(time_12h);
    gtk_label_set_xalign(GTK_LABEL(time_label), 0.0); gtk_widget_add_css_class(time_label, "upcoming-event-time");
    gtk_box_append(GTK_BOX(details_box), title_label); gtk_box_append(GTK_BOX(details_box), time_label);
    gtk_box_append(GTK_BOX(hbox), date_box); gtk_box_append(GTK_BOX(hbox), details_box); return row;
}

static gint sort_upcoming_events(gconstpointer a, gconstpointer b) { UpcomingEvent *ue_a = (UpcomingEvent *)a; UpcomingEvent *ue_b = (UpcomingEvent *)b; return g_date_time_compare(ue_a->datetime, ue_b->datetime); }
static void populate_upcoming_events_list(CalendarApp *app) {
    if (!app->events && !app->permanent_events) return;
    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(app->upcoming_list_box));
    while(child) { GtkWidget *next_child = gtk_widget_get_next_sibling(child); gtk_list_box_remove(app->upcoming_list_box, child); child = next_child; }
    
    GList *upcoming_list = NULL;
    GDateTime *now = g_date_time_new_now_local();
    GDateTime *today = g_date_time_new(g_date_time_get_timezone(now), g_date_time_get_year(now), g_date_time_get_month(now), g_date_time_get_day_of_month(now), 0, 0, 0);
    
    if (app->events) {
        GHashTableIter iter; gpointer key, value;
        g_hash_table_iter_init(&iter, app->events);
        while(g_hash_table_iter_next(&iter, &key, &value)) {
            int y, m, d; sscanf((gchar *)key, "%d-%d-%d", &y, &m, &d);
            GDateTime *event_date = g_date_time_new(g_date_time_get_timezone(today), y, m, d, 0, 0, 0);
            if (g_date_time_compare(event_date, today) >= 0) {
                for (GList *l = (GList *)value; l != NULL; l = l->next) {
                    UpcomingEvent *ue = g_new(UpcomingEvent, 1);
                    ue->datetime = g_date_time_ref(event_date);
                    ue->event = (Event *)l->data;
                    upcoming_list = g_list_prepend(upcoming_list, ue);
                }
            }
            g_date_time_unref(event_date);
        }
    }

    if (app->permanent_events) {
        GHashTableIter iter; gpointer key, value;
        g_hash_table_iter_init(&iter, app->permanent_events);
        while(g_hash_table_iter_next(&iter, &key, &value)) {
            int m, d; sscanf((gchar *)key, "%d-%d", &m, &d);
            GDateTime *event_date_this_year = g_date_time_new(g_date_time_get_timezone(today), g_date_time_get_year(today), m, d, 0, 0, 0);
            GDateTime *next_occurrence = NULL;
            if (g_date_time_compare(event_date_this_year, today) >= 0) {
                next_occurrence = g_date_time_ref(event_date_this_year);
            } else {
                next_occurrence = g_date_time_add_years(event_date_this_year, 1);
            }
            g_date_time_unref(event_date_this_year);

            for (GList *l = (GList *)value; l != NULL; l = l->next) {
                UpcomingEvent *ue = g_new(UpcomingEvent, 1);
                ue->datetime = g_date_time_ref(next_occurrence);
                ue->event = (Event *)l->data;
                upcoming_list = g_list_prepend(upcoming_list, ue);
            }
            g_date_time_unref(next_occurrence);
        }
    }

    g_date_time_unref(now);
    g_date_time_unref(today);
    
    upcoming_list = g_list_sort(upcoming_list, (GCompareFunc)sort_upcoming_events);
    for(GList *l = upcoming_list; l != NULL; l = l->next) {
        GtkWidget *row = create_upcoming_event_row((UpcomingEvent *)l->data);
        gtk_list_box_append(app->upcoming_list_box, row);
    }
    g_list_free_full(upcoming_list, free_upcoming_event);
}

// --- Main Application Setup ---
static void on_app_shutdown(GtkApplication *gtk_app, gpointer user_data) {
    (void)gtk_app; CalendarApp *app = (CalendarApp *)user_data;
    if (app->grid_population_timer_id > 0) g_source_remove(app->grid_population_timer_id);
    GridPopulationState *state = g_object_get_data(G_OBJECT(app->calendar_grid), "population-state");
    if (state) g_free(state);
    if (app->events) g_hash_table_unref(app->events);
    if (app->permanent_events) g_hash_table_unref(app->permanent_events);
    if (app->current_date) g_date_time_unref(app->current_date);
    g_free(app);
}

static void on_focus_lost(GtkWindow *window, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    if (!gtk_window_is_active(window)) {
        g_application_quit(G_APPLICATION(user_data));
    }
}

static gboolean start_grid_population_idle(gpointer user_data) {
    start_grid_population((CalendarApp *)user_data);
    return G_SOURCE_REMOVE;
}

static void on_add_event_popover_closed(GtkPopover *popover, gpointer user_data) {
    (void)user_data;
    if (gtk_widget_get_parent(GTK_WIDGET(popover))) {
        gtk_widget_unparent(GTK_WIDGET(popover));
    }
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    (void)user_data;
    GtkWidget *window = gtk_application_window_new(gtk_app);
    CalendarApp *app = g_new0(CalendarApp, 1);
    app->main_window = GTK_WINDOW(window);

    app->css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(app->css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_css_provider_load_from_path(app->css_provider, "src/style.css");
    
    GFile *css_file = g_file_new_for_path("src/style.css");
    GFileMonitor *monitor = g_file_monitor_file(css_file, G_FILE_MONITOR_NONE, NULL, NULL);
    g_signal_connect(monitor, "changed", G_CALLBACK(on_css_file_changed), app);
    g_object_unref(css_file);

    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, 10);
    gtk_widget_add_css_class(window, "layer-shell-window");

    GtkWidget *calendar_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(calendar_vbox, APP_WIDTH, -1);
    gtk_window_set_child(GTK_WINDOW(window), calendar_vbox);
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(header_box, "header-box");
    gtk_box_append(GTK_BOX(calendar_vbox), header_box);
    GtkWidget *prev_button = gtk_button_new_with_label("‹");
    gtk_widget_add_css_class(prev_button, "nav-button");
    gtk_box_append(GTK_BOX(header_box), prev_button);
    GtkWidget *month_menu_button = gtk_menu_button_new();
    gtk_widget_add_css_class(month_menu_button, "month-button");
    gtk_widget_set_hexpand(month_menu_button, TRUE);
    gtk_box_append(GTK_BOX(header_box), month_menu_button);
    app->month_label = GTK_LABEL(gtk_label_new("..."));
    gtk_widget_add_css_class(GTK_WIDGET(app->month_label), "month-label");
    gtk_menu_button_set_child(GTK_MENU_BUTTON(month_menu_button), GTK_WIDGET(app->month_label));
    GtkWidget *next_button = gtk_button_new_with_label("›");
    gtk_widget_add_css_class(next_button, "nav-button");
    gtk_box_append(GTK_BOX(header_box), next_button);
    GtkWidget *upcoming_popover = gtk_popover_new();
    gtk_widget_add_css_class(upcoming_popover, "event-popover");
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(month_menu_button), upcoming_popover);
    GtkWidget *panel_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(panel_container, "upcoming-panel");
    gtk_popover_set_child(GTK_POPOVER(upcoming_popover), panel_container);
    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scrolled_window, -1, 300);
    gtk_box_append(GTK_BOX(panel_container), scrolled_window);
    app->upcoming_list_box = GTK_LIST_BOX(gtk_list_box_new());
    gtk_widget_add_css_class(GTK_WIDGET(app->upcoming_list_box), "upcoming-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), GTK_WIDGET(app->upcoming_list_box));
    app->calendar_grid = GTK_GRID(gtk_grid_new());
    gtk_widget_add_css_class(GTK_WIDGET(app->calendar_grid), "calendar-grid");
    gtk_box_append(GTK_BOX(calendar_vbox), GTK_WIDGET(app->calendar_grid));
    const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    for (int i=0; i < 7; i++) {
        GtkWidget *label = gtk_label_new(weekdays[i]);
        gtk_widget_add_css_class(label, "weekday-label");
        gtk_grid_attach(app->calendar_grid, label, i, 0, 1, 1);
    }
    app->add_event_popover = GTK_POPOVER(gtk_popover_new());
    gtk_widget_add_css_class(GTK_WIDGET(app->add_event_popover), "event-popover");
    g_signal_connect(app->add_event_popover, "closed", G_CALLBACK(on_add_event_popover_closed), NULL);
    GtkWidget *popover_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(popover_vbox, 10); gtk_widget_set_margin_end(popover_vbox, 10);
    gtk_widget_set_margin_top(popover_vbox, 10); gtk_widget_set_margin_bottom(popover_vbox, 10);
    gtk_popover_set_child(app->add_event_popover, popover_vbox);
    app->add_event_title_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(app->add_event_title_entry, "Event Title (Required)");
    gtk_box_append(GTK_BOX(popover_vbox), GTK_WIDGET(app->add_event_title_entry));
    app->add_event_allday_check = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("All-day event"));
    gtk_box_append(GTK_BOX(popover_vbox), GTK_WIDGET(app->add_event_allday_check));
    app->add_event_time_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    const char *hours[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", NULL};
    app->add_event_hour_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(hours));
    const char *mins[] = {"00", "05", "10", "15", "20", "25", "30", "35", "40", "45", "50", "55", NULL};
    app->add_event_minute_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(mins));
    const char *am_pm[] = {"AM", "PM", NULL};
    app->add_event_ampm_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(am_pm));
    gtk_box_append(GTK_BOX(app->add_event_time_box), GTK_WIDGET(app->add_event_hour_dropdown));
    gtk_box_append(GTK_BOX(app->add_event_time_box), gtk_label_new(":"));
    gtk_box_append(GTK_BOX(app->add_event_time_box), GTK_WIDGET(app->add_event_minute_dropdown));
    gtk_box_append(GTK_BOX(app->add_event_time_box), GTK_WIDGET(app->add_event_ampm_dropdown));
    gtk_box_append(GTK_BOX(popover_vbox), app->add_event_time_box);
    GtkWidget *save_button = gtk_button_new_with_label("Save Event");
    gtk_box_append(GTK_BOX(popover_vbox), save_button);

    g_signal_connect(prev_button, "clicked", G_CALLBACK(on_prev_month_clicked), app);
    g_signal_connect(next_button, "clicked", G_CALLBACK(on_next_month_clicked), app);
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_add_event_save), app);
    g_signal_connect(app->add_event_allday_check, "notify::active", G_CALLBACK(on_allday_toggled), app);
    g_signal_connect(gtk_app, "shutdown", G_CALLBACK(on_app_shutdown), app);
    g_signal_connect(window, "notify::active", G_CALLBACK(on_focus_lost), gtk_app);

    app->current_date = g_date_time_new_now_local();
    g_idle_add(start_grid_population_idle, app);
    GTask *task = g_task_new(gtk_app, NULL, on_data_loaded, app);
    g_task_run_in_thread(task, load_data_in_thread);
    g_object_unref(task);

    g_signal_connect(window, "map", G_CALLBACK(start_slide_in_animation), NULL);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.meismeric.sleekcalendar", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}