//#include <gtk4-layer-shell.h>
#include <adwaita.h>
#include "popout_panels.h"
#include "control_center.h"
#include "utils.h"
#include <glib/gregex.h>

#define MAIN_SIDEBAR_WIDTH 66

// --- Structs for this file ---
// This is the main state-tracking struct for the entire connection process.
typedef struct {
    gchar *ssid;
    gchar *password; // This will be NULL for the initial silent attempt.
    GtkWidget *list_box;
    AdwToastOverlay *toast_overlay;
    GtkWidget *parent_window; // So we know where to show the dialog if needed.
    int attempt_number;
} WifiAsyncData;

// A simple struct to pass data from the list to the click handler.
typedef struct {
    gchar *ssid;
    GtkWidget *list_box;
} WifiClickData;

typedef struct {
    guint default_id;
    GtkDropDown *dropdown;
} SinksUpdateData;

typedef struct { guint id; gchar *name; } AudioSink;
typedef struct { GtkBox *list_box; gchar *active_ssid; } WifiUpdateData;
typedef struct { gchar *mac_address; GtkWidget *list_box; } BtActionData;


// --- Forward Declarations ---
static void try_next_wifi_step(WifiAsyncData *data);
static void on_wifi_command_finished(GObject *source, GAsyncResult *res, gpointer user_data);
static void free_wifi_click_data(gpointer data, GClosure *closure);
static void show_password_dialog(WifiAsyncData *data);
static void on_password_dialog_response(GObject *source, GAsyncResult *result, gpointer user_data);

static void on_initial_volume_ready(GObject *s, GAsyncResult *res, gpointer d);
static void on_volume_changed(GtkRange *r, gpointer d);
static void on_initial_volume_ready(GObject *s, GAsyncResult *res, gpointer d);
static void on_volume_changed(GtkRange *r, gpointer d);
static void on_get_default_sink_ready(GObject *s, GAsyncResult *res, gpointer d); // <-- ADD THIS
static void on_sinks_ready(GObject *s, GAsyncResult *res, gpointer d);
static void on_sinks_ready(GObject *s, GAsyncResult *res, gpointer d);
static void on_sink_selected(GtkDropDown *dd, GParamSpec *p, gpointer d);
static void on_sink_set_finished(GObject *s, GAsyncResult *res, gpointer d);
static void on_wifi_scan_ready(GObject *source_object, GAsyncResult *res, gpointer user_data);
static void on_wifi_connect_clicked(GtkButton *button, gpointer user_data);
static gboolean trigger_wifi_scan_refresh(gpointer user_data);
static void on_wifi_panel_destroy(GtkWidget *widget, gpointer user_data);
static void on_wifi_panel_state_set(GtkSwitch *sw, gboolean state, gpointer d);
static void on_wifi_panel_status_ready(GObject *s, GAsyncResult *res, gpointer d);
static void free_audio_sink(gpointer d);
static void free_bt_action_data(gpointer d, GClosure *closure);
static gboolean trigger_bt_refresh(gpointer user_data);
static void refresh_audio_panel_sinks(void);
static void on_bt_list_needs_refresh(GObject *s, GAsyncResult *res, gpointer d);
static void on_bt_connect_clicked(GtkButton *button, gpointer user_data);
static void on_bt_disconnect_clicked(GtkButton *button, gpointer user_data);
static void on_bt_paired_devices_ready(GObject *s, GAsyncResult *res, gpointer d);
static void on_bt_connected_devices_ready(GObject *s, GAsyncResult *res, gpointer d);
static void on_bt_panel_state_set(GtkSwitch *sw, gboolean state, gpointer d);
static void on_bt_panel_status_ready(GObject *s, GAsyncResult *res, gpointer d);
static void on_bt_panel_destroy(GtkWidget *widget, gpointer user_data);


// --- Helper Functions ---
static void free_wifi_click_data(gpointer data, GClosure *closure) {
    (void)closure;
    WifiClickData *d = data;
    g_free(d->ssid);
    g_free(d);
}

// --- Audio Callbacks ---
static void on_sink_set_finished(GObject *s, GAsyncResult *res, gpointer d) {
    (void)s; (void)res;
    if (d)
        execute_command_async("wpctl get-volume @DEFAULT_SINK@", on_initial_volume_ready, ((ControlWidgets*)d)->volume_slider);
}
static void on_volume_changed(GtkRange *r, gpointer d) {
    (void)d;
    int v = (int)gtk_range_get_value(r);
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "wpctl set-volume @DEFAULT_SINK@ %d%%", v);
    execute_command_async(cmd, NULL, NULL);
}
static void on_initial_volume_ready(GObject *s, GAsyncResult *res, gpointer d) {
    (void)s;
    GtkScale *vs = GTK_SCALE(d);
    const char *out = get_command_stdout(res);
    if (!out) return;
    if (strstr(out, "[MUTED]")) {
        gtk_range_set_value(GTK_RANGE(vs), 0);
        return;
    }
    const char *vol_s = strstr(out, "Volume: ");
    if (vol_s) {
        double v_f = atof(vol_s + 8);
        g_signal_handlers_block_by_func(vs, G_CALLBACK(on_volume_changed), NULL);
        gtk_range_set_value(GTK_RANGE(vs), v_f * 100);
        g_signal_handlers_unblock_by_func(vs, G_CALLBACK(on_volume_changed), NULL);
    }
}
static void free_audio_sink(gpointer data) {
    AudioSink *s = data;
    g_free(s->name);
    g_free(s);
}
static void on_sink_selected(GtkDropDown *dd, GParamSpec *p, gpointer d) {
    (void)p;
    ControlWidgets *w = d;
    GList *sinks = g_object_get_data(G_OBJECT(dd), "sinks-list");
    if (!sinks || !w) return;
    if (gtk_widget_get_realized(GTK_WIDGET(dd))) {
        guint idx = gtk_drop_down_get_selected(dd);
        AudioSink *sel = g_list_nth_data(sinks, idx);
        if (sel) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "wpctl set-default %u", sel->id);
            execute_command_async(cmd, on_sink_set_finished, w);
        }
    }
}

static void on_get_default_sink_ready(GObject *s, GAsyncResult *res, gpointer d) {
    (void)s;
    GtkDropDown *dd = GTK_DROP_DOWN(d);
    const char *out = get_command_stdout(res);
    if (!out) return;

    SinksUpdateData *update_data = g_new0(SinksUpdateData, 1);
    update_data->dropdown = dd;
    update_data->default_id = 0; // Default to 0 if not found

    // This simple regex reliably finds the ID from "Audio/Sink   52"
    g_autofree gchar *id_str = NULL;
    GRegex *default_regex = g_regex_new("Audio/Sink\\s+([0-9]+)", 0, 0, NULL);
    if (default_regex) {
        GMatchInfo *mi = NULL;
        if (g_regex_match(default_regex, out, 0, &mi)) {
            id_str = g_match_info_fetch(mi, 1);
            if (id_str) {
                update_data->default_id = (guint)atoi(id_str);
            }
        }
        g_match_info_free(mi);
        g_regex_unref(default_regex);
    }

    // Now that we have the default ID, get the full list of sinks.
    execute_command_async("wpctl status", on_sinks_ready, update_data);
}

static void on_sinks_ready(GObject *s, GAsyncResult *res, gpointer d) {
    (void)s;
    GtkDropDown *dd = GTK_DROP_DOWN(d);
    const char *o = get_command_stdout(res);
    if (!o) return;

    GList *sinks = NULL;
    GtkStringList *model = gtk_string_list_new(NULL);
    guint default_idx = 0;
    guint current_idx = 0;
    gboolean in_sinks_section = FALSE;

    gchar **lines = g_strsplit(o, "\n", -1);

    for (int i = 0; lines[i] != NULL; i++) {
        const char *line = lines[i];

        // This is the C version of: awk '/Sinks:/, /Sources:/'
        if (strstr(line, "Sinks:")) {
            in_sinks_section = TRUE;
            continue;
        }
        if (in_sinks_section && strstr(line, "Sources:")) {
            break; // We've reached the end of the sinks section
        }
        if (!in_sinks_section) {
            continue;
        }
        // End of awk logic

        // This is the C version of: grep -v 'Easy Effects Sink'
        if (strstr(line, "Easy Effects Sink")) {
            continue;
        }
        
        // This is the C version of: grep -E '[0-9]+\.'
        // We look for the dot. If it's not there, it's not a device line.
        const char *dot = strchr(line, '.');
        if (!dot) {
            continue;
        }

        // --- Now we have a valid line, parse it like the shell script ---
        gboolean is_default = (strstr(line, "*") != NULL);

        // Get the ID (like `awk '{print $1}' | tr -d '.'`)
        const char *id_start = strpbrk(line, "0123456789");
        if (!id_start) continue; // Should be impossible due to above check, but safe
        g_autofree gchar *id_str = g_strndup(id_start, dot - id_start);
        guint current_id = (guint)atoi(id_str);

        // Get the name (like `sed ... | xargs`)
        const char *name_start = dot + 1;
        const char *name_end = strstr(name_start, "[vol:");
        
        gchar *temp_name;
        if (name_end) {
            temp_name = g_strndup(name_start, name_end - name_start);
        } else {
            temp_name = g_strdup(name_start);
        }
        gchar *clean_name = g_strstrip(temp_name);
        
        // At this point, `clean_name` is our final, perfect device name.

        if (is_default) {
            default_idx = current_idx;
        }

        AudioSink *sink = g_new0(AudioSink, 1);
        sink->id = current_id;
        sink->name = g_strdup(clean_name); // Use the final cleaned name
        sinks = g_list_append(sinks, sink);
        gtk_string_list_append(model, sink->name);
        current_idx++;
        
        g_free(temp_name); // Clean up the temporary string
    }
    g_strfreev(lines);

    // Update the UI
    g_signal_handlers_block_by_func(dd, G_CALLBACK(on_sink_selected), NULL);
    gtk_drop_down_set_model(dd, G_LIST_MODEL(model));
    if (gtk_string_list_get_string(model, 0) != NULL) {
        gtk_drop_down_set_selected(dd, default_idx);
    }
    g_object_set_data_full(G_OBJECT(dd), "sinks-list", sinks, (GDestroyNotify)free_audio_sink);
    g_signal_handlers_unblock_by_func(dd, G_CALLBACK(on_sink_selected), NULL);
}

// --- Wi-Fi Callbacks ---

// The user clicks a network in the list. This is the new entry point.
static void on_wifi_connect_clicked(GtkButton *button, gpointer user_data) {
    WifiClickData *click_data = user_data;
    GtkWidget *win = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW);

    if (!win) {
        g_warning("Could not find parent window to start Wi-Fi connection.");
        return;
    }
    
    g_print("Starting silent connection attempt for SSID: %s\n", click_data->ssid);

    // Create the data structure that will live through the entire process.
    WifiAsyncData *async_data = g_new0(WifiAsyncData, 1);
    async_data->ssid = g_strdup(click_data->ssid);
    async_data->password = NULL; // We start with no password.
    async_data->list_box = click_data->list_box;
    async_data->toast_overlay = ADW_TOAST_OVERLAY(g_object_get_data(G_OBJECT(win), "toast-overlay"));
    async_data->parent_window = win;
    async_data->attempt_number = 0;
    
    // Start the silent connection state machine.
    try_next_wifi_step(async_data);
}

// The core state machine.
static void try_next_wifi_step(WifiAsyncData *data) {
    g_autoptr(GError) error = NULL;
    GSubprocess *process = NULL;
    // Fix: Moved process creation inside each case to avoid dangling pointers
    
    switch (data->attempt_number) {
        case 0: { // Attempt 0: Try to bring an existing connection 'up'.
            g_print("Wi-Fi Attempt 0: Trying 'nmcli connection up' for SSID '%s'\n", data->ssid);
            const gchar *argv[] = {"nmcli", "connection", "up", data->ssid, NULL};
            process = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_NONE, &error);
            break;
        }
        case 1: { // Attempt 1: Delete any potentially broken existing connection profile.
            g_print("Wi-Fi Attempt 1: Trying to delete existing profile for SSID '%s'\n", data->ssid);
            const gchar *argv[] = {"nmcli", "connection", "delete", data->ssid, NULL};
            process = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_NONE, &error);
            break;
        }
        case 2: { // Attempt 2: Add a new connection profile.
            g_print("Wi-Fi Attempt 2: Adding new connection profile for SSID '%s'\n", data->ssid);
            if (data->password && *data->password) {
                const gchar *argv[] = {
                    "nmcli", "connection", "add", "type", "wifi", 
                    "con-name", data->ssid, "ifname", "wlan0", "ssid", data->ssid,
                    "--",
                    "wifi-sec.key-mgmt", "wpa-psk", 
                    "wifi-sec.psk", data->password,
                    NULL
                };
                process = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_NONE, &error);
            } else {
                const gchar *argv[] = {
                    "nmcli", "connection", "add", "type", "wifi", 
                    "con-name", data->ssid, "ifname", "wlan0", "ssid", data->ssid,
                    NULL
                };
                process = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_NONE, &error);
            }
            break;
        }
        case 3: { // Attempt 3: Explicitly bring up the newly created profile.
            g_print("Wi-Fi Attempt 3: Bringing up new connection for SSID '%s'\n", data->ssid);
            const gchar *argv[] = {"nmcli", "connection", "up", data->ssid, NULL};
            process = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_NONE, &error);
            break;
        }
        default: // All silent/automated attempts have failed.
            if (data->password) {
                 g_warning("All Wi-Fi connection methods failed for SSID '%s', even with password.\n", data->ssid);
                 adw_toast_overlay_add_toast(data->toast_overlay, adw_toast_new("Connection failed. Incorrect password?"));
                 g_free(data->ssid);
                 g_free(data->password);
                 g_free(data);
            } else {
                g_print("Silent attempts failed. Prompting for password for SSID: %s\n", data->ssid);
                show_password_dialog(data);
            }
            return; // Stop the state machine for now.
    }

    if (error) {
        g_warning("Failed to spawn nmcli process: %s\n", error->message);
        data->attempt_number++;
        try_next_wifi_step(data);
        return;
    }
    g_subprocess_wait_check_async(process, NULL, (GAsyncReadyCallback)on_wifi_command_finished, data);
}


// This function is called after each command finishes.
static void on_wifi_command_finished(GObject *source, GAsyncResult *res, gpointer user_data) {
    WifiAsyncData *data = user_data;
    g_autoptr(GError) error = NULL;
    gboolean success = g_subprocess_wait_check_finish(G_SUBPROCESS(source), res, &error);

    if (success) {
        // Any successful 'up' is a final victory.
        if (data->attempt_number == 0 || data->attempt_number == 3) {
            g_print("Wi-Fi connection successful for SSID: %s\n", data->ssid);
            adw_toast_overlay_add_toast(data->toast_overlay, adw_toast_new_format("Connected to %s", data->ssid));
            trigger_wifi_scan_refresh(data->list_box);
            g_free(data->ssid);
            g_free(data->password);
            g_free(data);
            return;
        }
    }
    
    // Case 1: 'delete' (attempt 1) can fail, that's fine. Always proceed.
    // Case 2: 'add' (attempt 2) MUST succeed to continue.
    if (data->attempt_number == 1 || (data->attempt_number == 2 && success)) {
        data->attempt_number++;
        try_next_wifi_step(data);
        return;
    }
    
    if (!success) {
        g_warning("Wi-Fi attempt %d failed for SSID '%s': %s\n", data->attempt_number, data->ssid, error ? error->message : "unknown reason");
    }
    data->attempt_number++;
    try_next_wifi_step(data);
}

// This new function shows the password dialog as a last resort.
static void show_password_dialog(WifiAsyncData *data) {
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new("Connection Failed", NULL));
    
    adw_alert_dialog_add_response(dialog, "cancel", "Cancel");
    adw_alert_dialog_add_response(dialog, "connect", "Connect");
    adw_alert_dialog_set_response_appearance(dialog, "connect", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dialog, "connect");

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_margin_start(grid, 10);
    gtk_widget_set_margin_end(grid, 10);
    gtk_widget_set_margin_top(grid, 10);
    gtk_widget_set_margin_bottom(grid, 10);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Could not connect. Please enter the password."), 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("SSID:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(data->ssid), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Password:"), 0, 2, 1, 1);
    GtkWidget *entry = gtk_password_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), entry, 1, 2, 1, 1);
    gtk_widget_grab_focus(entry);

    adw_alert_dialog_set_extra_child(dialog, grid);
    
    g_object_set_data(G_OBJECT(dialog), "password-entry", entry);
    adw_alert_dialog_choose(dialog, GTK_WIDGET(data->parent_window), NULL, (GAsyncReadyCallback)on_password_dialog_response, data);
}

// This is called after the user responds to the password dialog.
static void on_password_dialog_response(GObject *source, GAsyncResult *result, gpointer user_data) {
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(source);
    WifiAsyncData *data = user_data;
    GtkEditable *entry = g_object_get_data(G_OBJECT(dialog), "password-entry");
    const gchar *response_id = adw_alert_dialog_choose_finish(dialog, result);

    if (g_str_equal(response_id, "connect")) {
        const gchar *password_text = gtk_editable_get_text(entry);
        
        if (!password_text || !*password_text) {
             g_print("Password dialog submitted with no password. Cancelling.\n");
             g_free(data->ssid);
             g_free(data->password);
             g_free(data);
             return;
        }

        g_print("Retrying connection with user-provided password.\n");
        g_free(data->password);
        data->password = g_strdup(password_text);
        data->attempt_number = 1; // Restart the process, skipping the first 'up' attempt.
        try_next_wifi_step(data);
    } else {
        // User clicked "Cancel", so we clean up.
        g_print("Password dialog cancelled.\n");
        g_free(data->ssid);
        g_free(data->password);
        g_free(data);
    }
}

static void on_wifi_scan_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    (void)source_object;
    GtkBox *box = GTK_BOX(user_data);
    const char *out = get_command_stdout(res);
    if (!out) return;

    gchar *active_ssid = NULL;
    gchar **lines = g_strsplit(out, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        if (g_str_has_prefix(lines[i], "yes:")) {
            gchar **parts = g_strsplit(lines[i], ":", 2);
            if (g_strv_length(parts) > 1) {
                active_ssid = g_strdup(parts[1]);
            }
            g_strfreev(parts);
            break;
        }
    }

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(box)))) {
        gtk_box_remove(box, child);
    }

    for (int i = 0; lines[i] && *lines[i]; i++) {
        gchar **parts = g_strsplit(lines[i], ":", 2);
        const char *ssid = parts[1];
        if (!ssid || !*ssid) { g_strfreev(parts); continue; }

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        
        WifiClickData *click_data = g_new(WifiClickData, 1);
        click_data->ssid = g_strdup(ssid);
        click_data->list_box = GTK_WIDGET(box);

        if (active_ssid && g_str_equal(ssid, active_ssid)) {
            GtkWidget *label = gtk_label_new(NULL);
            char *markup = g_strdup_printf("<b>%s</b>", ssid);
            gtk_label_set_markup(GTK_LABEL(label), markup);
            g_free(markup);
            gtk_widget_set_hexpand(label, TRUE);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            
            GtkWidget *button = gtk_button_new_with_label("Disconnect");
            
            g_free(click_data->ssid);
            click_data->ssid = g_strdup("");
            g_signal_connect_data(button, "clicked", G_CALLBACK(on_wifi_connect_clicked),
                                  click_data, (GClosureNotify)free_wifi_click_data, 0);

            gtk_box_append(GTK_BOX(row), label);
            gtk_box_append(GTK_BOX(row), button);
        } else {
            GtkWidget *button = gtk_button_new_with_label(ssid);
            gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
            gtk_widget_set_hexpand(button, TRUE);
            GtkWidget *label_child = gtk_button_get_child(GTK_BUTTON(button));
            if (GTK_IS_LABEL(label_child)) {
                gtk_label_set_xalign(GTK_LABEL(label_child), 0.0);
            }
            
            g_signal_connect_data(button, "clicked", G_CALLBACK(on_wifi_connect_clicked),
                                  click_data, (GClosureNotify)free_wifi_click_data, 0);

            gtk_box_append(GTK_BOX(row), button);
        }
        gtk_box_append(GTK_BOX(box), row);
        g_strfreev(parts);
    }
    g_strfreev(lines);
    g_free(active_ssid);
}

static gboolean trigger_wifi_scan_refresh(gpointer user_data) {
    GtkBox *box = GTK_BOX(user_data);
    if (!gtk_widget_get_visible(GTK_WIDGET(box))) return G_SOURCE_REMOVE;
    execute_command_async("nmcli -t -f active,ssid dev wifi", on_wifi_scan_ready, box);
    return G_SOURCE_CONTINUE;
}

static void on_wifi_panel_destroy(GtkWidget *widget, gpointer user_data) {
    (void)user_data;
    guint timer_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "wifi-refresh-timer-id"));
    if (timer_id) g_source_remove(timer_id);
}

static void on_wifi_panel_state_set(GtkSwitch *sw, gboolean state, gpointer d) {
    (void)sw; (void)d;
    execute_command_async(state ? "nmcli radio wifi on" : "nmcli radio wifi off", NULL, NULL);
}

static void on_wifi_panel_status_ready(GObject *s, GAsyncResult *res, gpointer d) {
    (void)s;
    GtkSwitch *sw = GTK_SWITCH(d);
    const char *out = get_command_stdout(res);
    gtk_switch_set_active(sw, out && strcmp(out, "enabled\n") == 0);
}


// --- Bluetooth Callbacks ---
static void free_bt_action_data(gpointer d, GClosure *closure) {
    (void)closure;
    BtActionData *ad = d;
    g_free(ad->mac_address);
    g_free(ad);
}

static void refresh_audio_panel_sinks(void) {
    // Find the audio panel window if it exists
    GtkApplication *app = GTK_APPLICATION(g_application_get_default());
    if (!app) return; // Safety check

    GList *windows = gtk_application_get_windows(app);
    for (GList *l = windows; l; l = l->next) {
        GtkWidget *win = l->data;
        // We identify the audio panel by the "widgets-bundle" we stored on it.
        ControlWidgets *widgets = g_object_get_data(G_OBJECT(win), "widgets-bundle");
        if (widgets && GTK_IS_DROP_DOWN(widgets->output_selector)) {
            g_print("Audio panel found. Refreshing sinks.\n");
            // Re-run the command to get the latest sinks and update the dropdown.
            execute_command_async("wpctl status", on_sinks_ready, widgets->output_selector);
            break; // Found it, no need to keep searching
        }
    }
    // No need to free the list returned by gtk_application_get_windows
}

static void on_bt_action_finished(GObject *s, GAsyncResult *res, gpointer d) {
    // First, trigger the standard refresh for the Bluetooth list.
    on_bt_list_needs_refresh(s, res, d);
    
    // Then, trigger the refresh for the audio panel's sinks.
    // We add a small delay to give the system time to register the new audio sink.
    g_timeout_add(1000, (GSourceFunc)refresh_audio_panel_sinks, NULL);
}

static void on_bt_connect_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    BtActionData *data = user_data;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "bluetoothctl connect %s", data->mac_address);
    execute_command_async(cmd, on_bt_action_finished, data->list_box);
}

static void on_bt_disconnect_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    BtActionData *data = user_data;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "bluetoothctl disconnect %s", data->mac_address);
    execute_command_async(cmd, on_bt_action_finished, data->list_box); // <-- NEW LINE (use new callback)
}

static void on_bt_paired_devices_ready(GObject *s, GAsyncResult *res, gpointer d) {
    (void)s;
    WifiUpdateData *data = d;
    const char *out = get_command_stdout(res);
    if (!out) { g_free(data->active_ssid); g_free(data); return; }
    GtkBox *list_box = data->list_box;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(list_box))))
        gtk_box_remove(list_box, child);
    gchar **lines = g_strsplit(out, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        gchar *mac = NULL, *name = NULL;
        if (sscanf(lines[i], "Device %ms %m[^\n]", &mac, &name) == 2) {
            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
            BtActionData *ad = g_new0(BtActionData, 1);
            ad->mac_address = g_strdup(mac);
            ad->list_box = GTK_WIDGET(list_box);
            if (data->active_ssid && strstr(data->active_ssid, mac)) {
                GtkWidget *label = gtk_label_new(NULL);
                char *markup = g_strdup_printf("<b>%s</b>", name);
                gtk_label_set_markup(GTK_LABEL(label), markup);
                g_free(markup);
                gtk_widget_set_hexpand(label, TRUE);
                gtk_label_set_xalign(GTK_LABEL(label), 0.0);
                GtkWidget *button = gtk_button_new_with_label("Disconnect");
                g_signal_connect_data(button, "clicked",
                                       G_CALLBACK(on_bt_disconnect_clicked),
                                       ad, (GClosureNotify)free_bt_action_data, 0);
                gtk_box_append(GTK_BOX(row), label);
                gtk_box_append(GTK_BOX(row), button);
            } else {
                GtkWidget *button = gtk_button_new_with_label(name);
                gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
                gtk_widget_set_hexpand(button, TRUE);
                gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(button))), 0.0);
                g_signal_connect_data(button, "clicked",
                                       G_CALLBACK(on_bt_connect_clicked),
                                       ad, (GClosureNotify)free_bt_action_data, 0);
                gtk_box_append(GTK_BOX(row), button);
            }
            gtk_box_append(GTK_BOX(list_box), row);
            g_free(mac); g_free(name);
        }
    }
    g_strfreev(lines);
    g_free(data->active_ssid);
    g_free(data);
}
static void on_bt_connected_devices_ready(GObject *s, GAsyncResult *res, gpointer d) {
    (void)s;
    const char *out = get_command_stdout(res);
    WifiUpdateData *data = g_new0(WifiUpdateData, 1);
    data->list_box = GTK_BOX(d);
    data->active_ssid = g_strdup(out);
    execute_command_async("bluetoothctl devices Paired", on_bt_paired_devices_ready, data);
}
static void on_bt_list_needs_refresh(GObject *s, GAsyncResult *res, gpointer d) {
    (void)s; (void)res;
    g_timeout_add_seconds(2, trigger_bt_refresh, d);
}
static gboolean trigger_bt_refresh(gpointer user_data) {
    GtkWidget *box = GTK_WIDGET(user_data);
    if (!gtk_widget_get_visible(box)) return G_SOURCE_REMOVE;
    execute_command_async("bluetoothctl devices Connected",
                          on_bt_connected_devices_ready, box);
    return G_SOURCE_CONTINUE;
}
static void on_bt_panel_state_set(GtkSwitch *sw, gboolean state, gpointer d) {
    (void)sw; (void)d;
    execute_command_async(state ? "bluetoothctl power on"
                                : "bluetoothctl power off",
                          NULL, NULL);
}
static void on_bt_panel_status_ready(GObject *s, GAsyncResult *res, gpointer d) {
    (void)s;
    GtkSwitch *bs = GTK_SWITCH(d);
    const char *o = get_command_stdout(res);
    gtk_switch_set_active(bs, o && strstr(o, "Powered: yes"));
}
static void on_bt_panel_destroy(GtkWidget *widget, gpointer user_data) {
    (void)user_data;
    guint timer_id = GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(widget), "bt-refresh-timer-id"));
    if (timer_id) g_source_remove(timer_id);
}

// --- Panel Creation Functions ---
GtkWidget* create_audio_panel(GtkApplication *app, GtkWidget *parent_window) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(parent_window));
    gtk_window_set_default_size(GTK_WINDOW(window), 250, -1);
   // gtk_layer_init_for_window(GTK_WINDOW(window));
   // gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);
   // gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
   // gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
   // gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, MAIN_SIDEBAR_WIDTH);
   // gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, top_margin); // MODIFIED
    GtkWidget *audio_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(audio_box, 10);
    gtk_widget_set_margin_end(audio_box, 10);
    gtk_widget_set_margin_top(audio_box, 10);
    gtk_widget_set_margin_bottom(audio_box, 10);
    ControlWidgets *widgets = g_new0(ControlWidgets, 1);
    widgets->volume_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_widget_set_hexpand(widgets->volume_slider, TRUE);
    g_signal_connect(widgets->volume_slider, "value-changed",
                     G_CALLBACK(on_volume_changed), NULL);
    gtk_box_append(GTK_BOX(audio_box), widgets->volume_slider);
    widgets->output_selector = gtk_drop_down_new(NULL, NULL);
    gtk_widget_set_hexpand(widgets->output_selector, TRUE);
    g_signal_connect(widgets->output_selector, "notify::selected",
                     G_CALLBACK(on_sink_selected), widgets);
    gtk_box_append(GTK_BOX(audio_box), widgets->output_selector);
    execute_command_async("wpctl get-volume @DEFAULT_SINK@",
                          on_initial_volume_ready,
                          widgets->volume_slider);
     execute_command_async("wpctl status", on_sinks_ready,
                          widgets->output_selector);
    gtk_window_set_child(GTK_WINDOW(window), audio_box);
    g_object_set_data_full(G_OBJECT(window), "widgets-bundle",
                          widgets, (GDestroyNotify)g_free);
    return window;
}

GtkWidget* create_wifi_panel(GtkApplication *app, GtkWidget *parent_window) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(parent_window));
    gtk_window_set_default_size(GTK_WINDOW(window), 280, 400);
    //gtk_layer_init_for_window(GTK_WINDOW(window));
    //gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);
    //gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    //gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    //gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, MAIN_SIDEBAR_WIDTH);
    //gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, top_margin); // MODIFIED

    AdwToastOverlay *toast_overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    g_object_set_data(G_OBJECT(window), "toast-overlay", toast_overlay);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_start(main_box, 10);
    gtk_widget_set_margin_end(main_box, 10);
    gtk_widget_set_margin_top(main_box, 10);
    gtk_widget_set_margin_bottom(main_box, 10);

    GtkWidget *toggle_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *toggle_label = gtk_label_new("Wi-Fi");
    GtkWidget *wifi_switch = gtk_switch_new();
    gtk_widget_set_hexpand(toggle_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(toggle_label), 0.0);
    g_signal_connect(wifi_switch, "state-set", G_CALLBACK(on_wifi_panel_state_set), NULL);
    execute_command_async("nmcli radio wifi", on_wifi_panel_status_ready, wifi_switch);
    gtk_box_append(GTK_BOX(toggle_box), toggle_label);
    gtk_box_append(GTK_BOX(toggle_box), wifi_switch);
    gtk_box_append(GTK_BOX(main_box), toggle_box);

    gtk_box_append(GTK_BOX(main_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_window, TRUE);

    GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), list_box);
    gtk_box_append(GTK_BOX(main_box), scrolled_window);

    guint timer_id = g_timeout_add_seconds(2, trigger_wifi_scan_refresh, list_box);
    g_object_set_data(G_OBJECT(window), "wifi-refresh-timer-id", GUINT_TO_POINTER(timer_id));
    g_signal_connect(window, "destroy", G_CALLBACK(on_wifi_panel_destroy), NULL);
    trigger_wifi_scan_refresh(list_box);

    adw_toast_overlay_set_child(toast_overlay, main_box);
    gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(toast_overlay));
    return window;
}

GtkWidget* create_bluetooth_panel(GtkApplication *app, GtkWidget *parent_window) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(parent_window));
    gtk_window_set_default_size(GTK_WINDOW(window), 280, 400);
    //gtk_layer_init_for_window(GTK_WINDOW(window));
    //gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);
    //gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    //gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    //gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, MAIN_SIDEBAR_WIDTH);
    //gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, top_margin); // MODIFIED

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_start(main_box, 10);
    gtk_widget_set_margin_end(main_box, 10);
    gtk_widget_set_margin_top(main_box, 10);
    gtk_widget_set_margin_bottom(main_box, 10);

    GtkWidget *toggle_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *toggle_label = gtk_label_new("Bluetooth");
    GtkWidget *bt_switch = gtk_switch_new();
    gtk_widget_set_hexpand(toggle_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(toggle_label), 0.0);
    g_signal_connect(bt_switch, "state-set", G_CALLBACK(on_bt_panel_state_set), NULL);
    execute_command_async("bluetoothctl show", on_bt_panel_status_ready, bt_switch);
    gtk_box_append(GTK_BOX(toggle_box), toggle_label);
    gtk_box_append(GTK_BOX(toggle_box), bt_switch);
    gtk_box_append(GTK_BOX(main_box), toggle_box);

    gtk_box_append(GTK_BOX(main_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), list_box);
    gtk_box_append(GTK_BOX(main_box), scrolled_window);

    guint timer_id = g_timeout_add_seconds(5, trigger_bt_refresh, list_box);
    g_object_set_data(G_OBJECT(window), "bt-refresh-timer-id", GUINT_TO_POINTER(timer_id));
    g_signal_connect(window, "destroy", G_CALLBACK(on_bt_panel_destroy), NULL);
    trigger_bt_refresh(list_box);

    gtk_window_set_child(GTK_WINDOW(window), main_box);
    return window;
}