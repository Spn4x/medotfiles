// ===== src/main.c =====
#include <gtk/gtk.h>
#include <adwaita.h>
#include <glib-unix.h>
#include "gtk4-layer-shell.h"
#include "wifi_scanner.h"
#include "bluetooth_scanner.h"
#include "network_manager.h"
#include "bluetooth_manager.h"
#include "audio_manager.h"
#include "brightness_manager.h"
#include "system_monitor.h"


// --- Configuration & AppWidgets Struct ---
const int WINDOW_WIDTH = 360;
const guint WIFI_SCAN_INTERVAL_SECONDS = 10;
const guint BT_SCAN_INTERVAL_SECONDS = 15;
const char* CSS_PATH = "src/style.css";
const int LIST_REQUESTED_HEIGHT = 155;

typedef struct {
    GtkWindow *main_window;
    GtkRevealer *stack_revealer;
    GtkStack *main_stack;
    GtkWidget *wifi_toggle, *bt_toggle, *audio_toggle;
    gulong wifi_toggle_handler_id, bt_toggle_handler_id, audio_toggle_handler_id;
    WifiScanner *wifi_scanner;
    BluetoothScanner *bt_scanner;
    SystemMonitor *system_monitor;
    GtkWidget *wifi_list_box, *wifi_list_overlay, *wifi_list_spinner;
    GtkWidget *bt_list_box, *bt_list_overlay, *bt_list_spinner;
    GtkWidget *audio_list_box;
    GtkWidget *system_volume_slider;
    gulong system_volume_handler_id;
    GtkWidget *brightness_slider;
    gulong brightness_slider_handler_id;
    gboolean airplane_mode_active;
    gboolean wifi_was_on_before_airplane;
    gboolean bt_was_on_before_airplane;
} AppWidgets;


// --- Forward Declarations ---
static gboolean rescan_after_delay(gpointer user_data);
static void on_wifi_operation_finished(gboolean success, gpointer user_data);
static void on_forget_button_clicked(GtkButton *button, GtkPopover *popover);
static void on_disconnect_button_clicked(GtkButton *button, GtkPopover *popover);
static void on_wifi_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);
static void on_wifi_network_clicked(GtkButton *button, gpointer user_data);
static void on_bt_operation_finished(gboolean success, gpointer user_data);
static void on_bt_disconnect_button_clicked(GtkButton *button, GtkPopover *popover);
static void on_bluetooth_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);
static void on_bluetooth_device_clicked(GtkButton *button, gpointer user_data);
static void on_system_volume_changed(GtkRange *range, gpointer user_data);
static GtkWidget* create_audio_page(AppWidgets *widgets);
static void on_sink_set_finished(gboolean success, gpointer user_data);
static void on_brightness_changed(GtkRange *range, gpointer user_data);
static void on_system_event(SystemEventType type, gpointer user_data);
static GtkWidget* create_list_entry(const char* icon, const char* label_text, gboolean is_active);
static void on_audio_sink_clicked(GtkButton *button, AudioSink *sink);
static GtkWidget* create_pill_slider(const char* icon_name);

// --- Core Functions ---
static void app_widgets_free(AppWidgets *widgets) {
    if (!widgets) return;
    wifi_scanner_free(widgets->wifi_scanner);
    bluetooth_scanner_free(widgets->bt_scanner);
    system_monitor_free(widgets->system_monitor);
    g_free(widgets);
}

void toggle_airplane_mode(GtkToggleButton *button, AppWidgets *widgets) {
    gboolean is_activating = gtk_toggle_button_get_active(button);
    widgets->airplane_mode_active = is_activating;
    if (is_activating) {
        g_print("Activating Airplane Mode...\n");
        widgets->wifi_was_on_before_airplane = is_wifi_enabled();
        widgets->bt_was_on_before_airplane = is_bluetooth_powered();
        if (widgets->wifi_was_on_before_airplane) { set_wifi_enabled_async(FALSE, NULL, NULL); }
        if (widgets->bt_was_on_before_airplane) { set_bluetooth_powered_async(FALSE, NULL, NULL); }
        gtk_revealer_set_reveal_child(widgets->stack_revealer, FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets->wifi_toggle), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets->bt_toggle), FALSE);
        gtk_widget_set_sensitive(widgets->wifi_toggle, FALSE);
        gtk_widget_set_sensitive(widgets->bt_toggle, FALSE);
    } else {
        g_print("Deactivating Airplane Mode...\n");
        if (widgets->wifi_was_on_before_airplane) { set_wifi_enabled_async(TRUE, NULL, NULL); }
        if (widgets->bt_was_on_before_airplane) { set_bluetooth_powered_async(TRUE, NULL, NULL); }
        gtk_widget_set_sensitive(widgets->wifi_toggle, TRUE);
        gtk_widget_set_sensitive(widgets->bt_toggle, TRUE);
    }
}

// --- Wi-Fi Operation Handlers ---
static gboolean rescan_after_delay(gpointer user_data) {
    AppWidgets *widgets = user_data;
    g_print("Delayed scan triggered after operation.\n");
    wifi_scanner_trigger_scan(widgets->wifi_scanner);
    return G_SOURCE_REMOVE;
}

static void on_wifi_operation_finished(gboolean success, gpointer user_data) {
    AppWidgets *widgets = user_data;
    g_print("Wi-Fi operation finished. Success: %d\n", success);
    gtk_spinner_stop(GTK_SPINNER(widgets->wifi_list_spinner));
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->wifi_list_overlay), TRUE);
    g_timeout_add(500, rescan_after_delay, widgets);
}

static void on_wifi_network_clicked(GtkButton *button, gpointer user_data) {
    WifiNetwork *net = (WifiNetwork*)user_data;
    AppWidgets *widgets = g_object_get_data(G_OBJECT(gtk_widget_get_root(GTK_WIDGET(button))), "app-widgets");

    if (net->is_active) {
        return; // Do nothing if already connected to it
    }

    gtk_widget_set_sensitive(GTK_WIDGET(widgets->wifi_list_overlay), FALSE);
    gtk_spinner_start(GTK_SPINNER(widgets->wifi_list_spinner));
    
    g_autofree gchar *existing_connection_path = find_connection_for_ssid(net->ssid);

    if (existing_connection_path) {
        // --- Case 1: Saved connection exists, just activate it. ---
        g_print("Found existing connection. Activating: %s\n", net->ssid);
        activate_wifi_connection_async(existing_connection_path, net->object_path, on_wifi_operation_finished, widgets);
    } else {
        // --- Case 2: No saved connection. Add a new one. ---
        // We pass NULL for the password. If the network is secure, NetworkManager's
        // secret agent will prompt the user for the password automatically.
        g_print("Requesting new connection (agent will prompt if needed): %s\n", net->ssid);
        add_and_activate_wifi_connection_async(net->ssid, net->object_path, NULL, net->is_secure, on_wifi_operation_finished, widgets);
    }
}


static void on_forget_button_clicked(GtkButton *button, GtkPopover *popover) {
    gtk_popover_popdown(popover);
    AppWidgets *widgets = g_object_get_data(G_OBJECT(gtk_widget_get_root(GTK_WIDGET(button))), "app-widgets");
    const char *ssid = g_object_get_data(G_OBJECT(button), "ssid-to-forget");
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->wifi_list_overlay), FALSE);
    gtk_spinner_start(GTK_SPINNER(widgets->wifi_list_spinner));
    forget_wifi_connection_async(ssid, on_wifi_operation_finished, widgets);
}

static void on_disconnect_button_clicked(GtkButton *button, GtkPopover *popover) {
    gtk_popover_popdown(popover);
    AppWidgets *widgets = g_object_get_data(G_OBJECT(gtk_widget_get_root(GTK_WIDGET(button))), "app-widgets");
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->wifi_list_overlay), FALSE);
    gtk_spinner_start(GTK_SPINNER(widgets->wifi_list_spinner));
    disconnect_wifi_async(on_wifi_operation_finished, widgets);
}

// --- Bluetooth Operation Handlers ---
static void on_bluetooth_device_clicked(GtkButton *button, gpointer user_data) {
    BluetoothDevice *dev = (BluetoothDevice*)user_data;
    AppWidgets *widgets = g_object_get_data(G_OBJECT(gtk_widget_get_root(GTK_WIDGET(button))), "app-widgets");
    if (dev->is_connected) return;
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->bt_list_overlay), FALSE);
    gtk_spinner_start(GTK_SPINNER(widgets->bt_list_spinner));
    connect_to_bluetooth_device_async(dev->address, on_bt_operation_finished, widgets);
}

static void on_bt_disconnect_button_clicked(GtkButton *button, GtkPopover *popover) {
    gtk_popover_popdown(popover);
    AppWidgets *widgets = g_object_get_data(G_OBJECT(gtk_widget_get_root(GTK_WIDGET(button))), "app-widgets");
    const char *address = g_object_get_data(G_OBJECT(button), "address-to-disconnect");
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->bt_list_overlay), FALSE);
    gtk_spinner_start(GTK_SPINNER(widgets->bt_list_spinner));
    disconnect_bluetooth_device_async(address, on_bt_operation_finished, widgets);
}

static void on_bt_operation_finished(gboolean success, gpointer user_data) {
    (void)success;
    AppWidgets *widgets = user_data;
    gtk_spinner_stop(GTK_SPINNER(widgets->bt_list_spinner));
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->bt_list_overlay), TRUE);
    bluetooth_scanner_trigger_scan(widgets->bt_scanner);
}

// --- Audio, Brightness, and System Event Handlers ---
static void update_audio_device_list(AppWidgets *widgets) {
    GtkWidget *list_box = widgets->audio_list_box;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(list_box))) { gtk_box_remove(GTK_BOX(list_box), child); }
    GList *sinks = get_audio_sinks();
    if (g_list_length(sinks) == 0) {
        gtk_box_append(GTK_BOX(list_box), gtk_label_new("No audio devices found."));
    } else {
        for (GList *l = sinks; l != NULL; l = l->next) {
            AudioSink *sink_from_scan = l->data;
            AudioSink *sink_copy = g_new0(AudioSink, 1);
            *sink_copy = *sink_from_scan;
            sink_copy->name = g_strdup(sink_from_scan->name);
            GtkWidget *entry_button = create_list_entry("audio-card-symbolic", sink_copy->name, sink_copy->is_default);
            g_signal_connect(entry_button, "clicked", G_CALLBACK(on_audio_sink_clicked), sink_copy);
            g_signal_connect_swapped(entry_button, "destroy", G_CALLBACK(audio_sink_free), sink_copy);
            gtk_box_append(GTK_BOX(list_box), entry_button);
        }
    }
    free_audio_sink_list(sinks);
}

static void on_sink_set_finished(gboolean success, gpointer user_data) {
    if (success) { update_audio_device_list(user_data); }
}

static void on_audio_sink_clicked(GtkButton *button, AudioSink *sink) {
    AppWidgets *widgets = g_object_get_data(G_OBJECT(gtk_widget_get_root(GTK_WIDGET(button))), "app-widgets");
    set_default_sink_async(sink->id, on_sink_set_finished, widgets);
}

static void on_system_volume_changed(GtkRange *range, gpointer user_data) {
    (void)user_data;
    gint value = (gint)gtk_range_get_value(range);
    set_default_sink_volume_async(value, NULL, NULL);
}

static void on_brightness_changed(GtkRange *range, gpointer user_data) {
    (void)user_data;
    gint value = (gint)gtk_range_get_value(range);
    set_brightness_async(value);
}

static void on_system_event(SystemEventType type, gpointer user_data) {
    AppWidgets *widgets = user_data;
    switch (type) {
        case SYSTEM_EVENT_VOLUME_CHANGED: {
            AudioSinkState *state = get_default_sink_state();
            if (state) {
                g_signal_handler_block(widgets->system_volume_slider, widgets->system_volume_handler_id);
                gtk_range_set_value(GTK_RANGE(widgets->system_volume_slider), state->volume);
                g_signal_handler_unblock(widgets->system_volume_slider, widgets->system_volume_handler_id);
                g_free(state);
            }
            break;
        }
        case SYSTEM_EVENT_BRIGHTNESS_CHANGED: {
            gint brightness = get_current_brightness();
            if (brightness >= 0) {
                g_signal_handler_block(widgets->brightness_slider, widgets->brightness_slider_handler_id);
                gtk_range_set_value(GTK_RANGE(widgets->brightness_slider), brightness);
                g_signal_handler_unblock(widgets->brightness_slider, widgets->brightness_slider_handler_id);
            }
            break;
        }
    }
}

// --- UI Construction ---
static GtkWidget* create_list_entry(const char* icon, const char* label_text, gboolean is_active) {
    GtkWidget *button = gtk_button_new();
    gtk_widget_add_css_class(button, "list-item-button");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_button_set_child(GTK_BUTTON(button), box);
    gtk_box_append(GTK_BOX(box), gtk_image_new_from_icon_name(icon));
    GtkWidget *label = gtk_label_new(label_text);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), label);
    if (is_active) {
        GtkWidget *symbol_label = gtk_label_new("◉");
        gtk_box_append(GTK_BOX(box), symbol_label);
    }
    return button;
}

static void show_popover(GtkWidget *parent_button, GtkWidget *menu_button, GtkPopover *popover) {
    gtk_popover_set_child(popover, menu_button);
    gtk_widget_set_parent(GTK_WIDGET(popover), parent_button);
    gtk_popover_popup(popover);
}

static void on_wifi_right_click(GtkGestureClick *g, int n, double x, double y, gpointer user_data) {
    (void)n; (void)x; (void)y;
    WifiNetwork *net = user_data;
    GtkWidget *button_widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    GtkWidget *menu_button;
    GtkPopover *popover = GTK_POPOVER(gtk_popover_new());
    if (net->is_active) {
        menu_button = gtk_button_new_with_label("Disconnect");
        g_signal_connect(menu_button, "clicked", G_CALLBACK(on_disconnect_button_clicked), popover);
    } else {
        menu_button = gtk_button_new_with_label("Forget");
        g_object_set_data(G_OBJECT(menu_button), "ssid-to-forget", (gpointer)net->ssid);
        g_signal_connect(menu_button, "clicked", G_CALLBACK(on_forget_button_clicked), popover);
    }
    gtk_widget_set_margin_top(menu_button, 6); gtk_widget_set_margin_bottom(menu_button, 6);
    gtk_widget_set_margin_start(menu_button, 6); gtk_widget_set_margin_end(menu_button, 6);
    show_popover(button_widget, menu_button, popover);
}

static void on_bluetooth_right_click(GtkGestureClick *g, int n, double x, double y, gpointer user_data) {
    (void)n; (void)x; (void)y;
    BluetoothDevice *dev = user_data;
    if (!dev->is_connected) return;
    GtkWidget *button_widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    GtkPopover *popover = GTK_POPOVER(gtk_popover_new());
    GtkWidget *menu_button = gtk_button_new_with_label("Disconnect");
    g_object_set_data(G_OBJECT(menu_button), "address-to-disconnect", (gpointer)dev->address);
    g_signal_connect(menu_button, "clicked", G_CALLBACK(on_bt_disconnect_button_clicked), popover);
    gtk_widget_set_margin_top(menu_button, 6); gtk_widget_set_margin_bottom(menu_button, 6);
    gtk_widget_set_margin_start(menu_button, 6); gtk_widget_set_margin_end(menu_button, 6);
    show_popover(button_widget, menu_button, popover);
}

static const char* get_wifi_icon_name_for_signal(int strength, gboolean is_secure) {
    if (strength > 80) return is_secure ? "network-wireless-signal-excellent-secure-symbolic" : "network-wireless-signal-excellent-symbolic";
    if (strength > 55) return is_secure ? "network-wireless-signal-good-secure-symbolic" : "network-wireless-signal-good-symbolic";
    if (strength > 30) return is_secure ? "network-wireless-signal-ok-secure-symbolic" : "network-wireless-signal-ok-symbolic";
    if (strength > 5)  return is_secure ? "network-wireless-signal-weak-secure-symbolic" : "network-wireless-signal-weak-symbolic";
    return is_secure ? "network-wireless-signal-none-secure-symbolic" : "network-wireless-signal-none-symbolic";
}

static void on_wifi_scan_results(GList *networks, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets*)user_data;
    GtkWidget *list_box = widgets->wifi_list_box;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(list_box)) != NULL) { gtk_box_remove(GTK_BOX(list_box), child); }

    if (!is_wifi_enabled()) {
        GtkWidget *label = gtk_label_new("Wi-Fi is turned off");
        gtk_widget_set_vexpand(label, TRUE);
        gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(list_box), label);
        free_wifi_network_list(networks);
        return;
    }

    if (networks == NULL || g_list_length(networks) == 0) {
        GtkWidget *label = gtk_label_new("No Wi-Fi networks found.");
        gtk_widget_set_vexpand(label, TRUE);
        gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(list_box), label);
        free_wifi_network_list(networks); 
        return;
    }

    for (GList *l = networks; l != NULL; l = l->next) {
        WifiNetwork *net_from_scan = l->data;
        WifiNetwork *net_copy = g_new0(WifiNetwork, 1);
        net_copy->ssid = g_strdup(net_from_scan->ssid);
        net_copy->object_path = g_strdup(net_from_scan->object_path);
        net_copy->strength = net_from_scan->strength;
        net_copy->is_secure = net_from_scan->is_secure;
        net_copy->is_active = net_from_scan->is_active;
        const char *icon_name = get_wifi_icon_name_for_signal(net_copy->strength, net_copy->is_secure);
        GtkWidget *entry_button = create_list_entry(icon_name, net_copy->ssid, net_copy->is_active);
        if (net_copy->is_active) {
            gtk_widget_add_css_class(entry_button, "active-network");
        }
        g_signal_connect(entry_button, "clicked", G_CALLBACK(on_wifi_network_clicked), net_copy);
        g_signal_connect_swapped(entry_button, "destroy", G_CALLBACK(wifi_network_free), net_copy);
        GtkGesture *right_click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
        g_signal_connect(right_click, "pressed", G_CALLBACK(on_wifi_right_click), net_copy);
        gtk_widget_add_controller(entry_button, GTK_EVENT_CONTROLLER(right_click));
        gtk_box_append(GTK_BOX(list_box), entry_button);
    }
    free_wifi_network_list(networks);
}

static void on_bt_scan_results(GList *devices, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets*)user_data;
    GtkWidget *list_box = widgets->bt_list_box;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(list_box)) != NULL) { gtk_box_remove(GTK_BOX(list_box), child); }
    if (g_list_length(devices) == 0) {
        gtk_box_append(GTK_BOX(list_box), gtk_label_new("No Bluetooth devices found."));
        free_bluetooth_device_list(devices); return;
    }
    for (GList *l = devices; l != NULL; l = l->next) {
        BluetoothDevice *dev_from_scan = l->data;
        BluetoothDevice *dev_copy = g_new0(BluetoothDevice, 1);
        dev_copy->address = g_strdup(dev_from_scan->address);
        dev_copy->name = g_strdup(dev_from_scan->name);
        dev_copy->is_connected = dev_from_scan->is_connected;
        const char *icon_name = "bluetooth-active-symbolic";
        GtkWidget *entry_button = create_list_entry(icon_name, dev_copy->name, dev_copy->is_connected);
        if (dev_copy->is_connected) {
            gtk_widget_add_css_class(entry_button, "active-network");
        }
        g_signal_connect(entry_button, "clicked", G_CALLBACK(on_bluetooth_device_clicked), dev_copy);
        g_signal_connect_swapped(entry_button, "destroy", G_CALLBACK(bluetooth_device_free), dev_copy);
        GtkGesture *right_click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
        g_signal_connect(right_click, "pressed", G_CALLBACK(on_bluetooth_right_click), dev_copy);
        gtk_widget_add_controller(entry_button, GTK_EVENT_CONTROLLER(right_click));
        gtk_box_append(GTK_BOX(list_box), entry_button);
    }
    free_bluetooth_device_list(devices);
}

static GtkWidget* create_wifi_page(AppWidgets *widgets) {
    GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(list_box, 8);
    widgets->wifi_list_box = list_box;
    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), list_box);
    gtk_widget_set_size_request(scrolled_window, -1, LIST_REQUESTED_HEIGHT);
    gtk_widget_set_vexpand(scrolled_window, FALSE);
    gtk_widget_set_valign(scrolled_window, GTK_ALIGN_START);
    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *spinner = gtk_spinner_new();
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(spinner, GTK_ALIGN_CENTER);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), scrolled_window);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), spinner);
    widgets->wifi_list_overlay = overlay;
    widgets->wifi_list_spinner = spinner;
    return overlay;
}

static GtkWidget* create_bluetooth_page(AppWidgets *widgets) {
    GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(list_box, 8);
    widgets->bt_list_box = list_box;
    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), list_box);
    gtk_widget_set_size_request(scrolled_window, -1, LIST_REQUESTED_HEIGHT);
    gtk_widget_set_vexpand(scrolled_window, FALSE);
    gtk_widget_set_valign(scrolled_window, GTK_ALIGN_START);
    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *spinner = gtk_spinner_new();
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(spinner, GTK_ALIGN_CENTER);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), scrolled_window);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), spinner);
    widgets->bt_list_overlay = overlay;
    widgets->bt_list_spinner = spinner;
    return overlay;
}

static GtkWidget* create_audio_page(AppWidgets *widgets) {
    GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(list_box, 8);
    widgets->audio_list_box = list_box;
    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), list_box);
    gtk_widget_set_size_request(scrolled_window, -1, LIST_REQUESTED_HEIGHT);
    gtk_widget_set_vexpand(scrolled_window, FALSE);
    gtk_widget_set_valign(scrolled_window, GTK_ALIGN_START);
    return scrolled_window;
}

static gboolean reveal_on_idle(gpointer user_data) {
    gtk_revealer_set_reveal_child(GTK_REVEALER(user_data), TRUE); return G_SOURCE_REMOVE;
}

static void on_expandable_toggle_toggled(GtkToggleButton *toggled_button, AppWidgets *widgets) {
    if (!gtk_toggle_button_get_active(toggled_button)) {
        gboolean any_active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgets->wifi_toggle)) ||
                              gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgets->bt_toggle)) ||
                              gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgets->audio_toggle));
        if (!any_active) {
            gtk_revealer_set_reveal_child(widgets->stack_revealer, FALSE);
            wifi_scanner_stop(widgets->wifi_scanner);
            bluetooth_scanner_stop(widgets->bt_scanner);
        }
        return;
    }
    const char *target_page = NULL;
    GtkWidget *other_toggle1 = NULL, *other_toggle2 = NULL;
    gulong handler_id1 = 0, handler_id2 = 0;
    wifi_scanner_stop(widgets->wifi_scanner);
    bluetooth_scanner_stop(widgets->bt_scanner);
    if (toggled_button == GTK_TOGGLE_BUTTON(widgets->wifi_toggle)) {
        target_page = "wifi_page";
        other_toggle1 = widgets->bt_toggle;     handler_id1 = widgets->bt_toggle_handler_id;
        other_toggle2 = widgets->audio_toggle;  handler_id2 = widgets->audio_toggle_handler_id;
        wifi_scanner_start(widgets->wifi_scanner, WIFI_SCAN_INTERVAL_SECONDS);
    } else if (toggled_button == GTK_TOGGLE_BUTTON(widgets->bt_toggle)) {
        target_page = "bt_page";
        other_toggle1 = widgets->wifi_toggle;   handler_id1 = widgets->wifi_toggle_handler_id;
        other_toggle2 = widgets->audio_toggle;  handler_id2 = widgets->audio_toggle_handler_id;
        bluetooth_scanner_start(widgets->bt_scanner, BT_SCAN_INTERVAL_SECONDS);
    } else if (toggled_button == GTK_TOGGLE_BUTTON(widgets->audio_toggle)) {
        target_page = "audio_page";
        other_toggle1 = widgets->wifi_toggle;   handler_id1 = widgets->wifi_toggle_handler_id;
        other_toggle2 = widgets->bt_toggle;     handler_id2 = widgets->bt_toggle_handler_id;
        update_audio_device_list(widgets);
    }
    g_signal_handler_block(other_toggle1, handler_id1);
    g_signal_handler_block(other_toggle2, handler_id2);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other_toggle1), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other_toggle2), FALSE);
    g_signal_handler_unblock(other_toggle1, handler_id1);
    g_signal_handler_unblock(other_toggle2, handler_id2);
    if (target_page) {
        gtk_stack_set_visible_child_name(widgets->main_stack, target_page);
        g_idle_add(reveal_on_idle, widgets->stack_revealer);
    }
}

static GtkWidget* create_square_toggle(const char* icon_name, const char* text) {
    GtkWidget *button = gtk_toggle_button_new(); gtk_widget_add_css_class(button, "square-toggle");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4); gtk_widget_set_halign(box, GTK_ALIGN_CENTER); gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_button_set_child(GTK_BUTTON(button), box); GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
    GtkWidget *label = gtk_label_new(text); gtk_box_append(GTK_BOX(box), icon); gtk_box_append(GTK_BOX(box), label); return button;
}

static GtkWidget* create_pill_slider(const char* icon_name) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12); gtk_widget_add_css_class(box, "pill-slider");
    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
    GtkWidget *slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE);
    gtk_widget_set_hexpand(slider, TRUE);
    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), slider);
    return box;
}

static gboolean initial_state_update(gpointer user_data) {
    AppWidgets *widgets = user_data;
    on_system_event(SYSTEM_EVENT_VOLUME_CHANGED, widgets);
    on_system_event(SYSTEM_EVENT_BRIGHTNESS_CHANGED, widgets);
    return G_SOURCE_REMOVE;
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    AppWidgets *widgets = g_new0(AppWidgets, 1);
    widgets->main_window = GTK_WINDOW(adw_application_window_new(app));
    gtk_window_set_title(widgets->main_window, "Control Center");
    gtk_window_set_default_size(widgets->main_window, WINDOW_WIDTH, -1);
    g_object_set_data(G_OBJECT(widgets->main_window), "app-widgets", widgets);

    // --- Layer Shell Configuration ---
    gtk_layer_init_for_window(widgets->main_window);
    gtk_layer_set_layer(widgets->main_window, GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_keyboard_mode(widgets->main_window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    // Anchor to the top and right edges to place it in the top-right corner.
    gtk_layer_set_anchor(widgets->main_window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(widgets->main_window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_margin(widgets->main_window, GTK_LAYER_SHELL_EDGE_TOP, 10);
    gtk_layer_set_margin(widgets->main_window, GTK_LAYER_SHELL_EDGE_RIGHT, 10);
    // --- End Layer Shell Configuration ---

    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(main_vbox, "main-popup");
    
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(widgets->main_window), main_vbox);

    GtkWidget *top_toggle_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(top_toggle_grid), 8); gtk_grid_set_row_spacing(GTK_GRID(top_toggle_grid), 8);
    gtk_box_append(GTK_BOX(main_vbox), top_toggle_grid);
    widgets->wifi_toggle = create_square_toggle("network-wireless-symbolic", "Wi-Fi");
    widgets->bt_toggle = create_square_toggle("bluetooth-active-symbolic", "Bluetooth");
    widgets->audio_toggle = create_square_toggle("audio-card-symbolic", "Audio");
    GtkWidget *airplane_toggle = create_square_toggle("airplane-mode-symbolic", "Airplane");
    gtk_grid_attach(GTK_GRID(top_toggle_grid), widgets->wifi_toggle, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(top_toggle_grid), widgets->bt_toggle, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(top_toggle_grid), widgets->audio_toggle, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(top_toggle_grid), airplane_toggle, 3, 0, 1, 1);
    GtkWidget *bottom_overlay = gtk_overlay_new();
    gtk_box_append(GTK_BOX(main_vbox), bottom_overlay);
    GtkWidget *sliders_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sliders_box, "sliders-box");
    gtk_box_append(GTK_BOX(sliders_box), gtk_label_new("System Volume"));
    GtkWidget *system_slider_box = create_pill_slider("audio-volume-high-symbolic");
    widgets->system_volume_slider = gtk_widget_get_last_child(system_slider_box);
    widgets->system_volume_handler_id = g_signal_connect(widgets->system_volume_slider, "value-changed", G_CALLBACK(on_system_volume_changed), NULL);
    gtk_box_append(GTK_BOX(sliders_box), system_slider_box);
    gtk_box_append(GTK_BOX(sliders_box), gtk_label_new("Brightness"));
    GtkWidget *brightness_slider_box = create_pill_slider("display-brightness-symbolic");
    widgets->brightness_slider = gtk_widget_get_last_child(brightness_slider_box);
    widgets->brightness_slider_handler_id = g_signal_connect(widgets->brightness_slider, "value-changed", G_CALLBACK(on_brightness_changed), NULL);
    gtk_box_append(GTK_BOX(sliders_box), brightness_slider_box);
    gtk_overlay_set_child(GTK_OVERLAY(bottom_overlay), sliders_box);
    widgets->stack_revealer = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(widgets->stack_revealer, GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_transition_duration(widgets->stack_revealer, 200);
    gtk_widget_set_valign(GTK_WIDGET(widgets->stack_revealer), GTK_ALIGN_START);
    widgets->main_stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_vhomogeneous(widgets->main_stack, FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(widgets->main_stack), "expandable-content-area");
    gtk_revealer_set_child(widgets->stack_revealer, GTK_WIDGET(widgets->main_stack));
    gtk_stack_add_named(widgets->main_stack, create_wifi_page(widgets), "wifi_page");
    gtk_stack_add_named(widgets->main_stack, create_bluetooth_page(widgets), "bt_page");
    gtk_stack_add_named(widgets->main_stack, create_audio_page(widgets), "audio_page");
    gtk_overlay_add_overlay(GTK_OVERLAY(bottom_overlay), GTK_WIDGET(widgets->stack_revealer));
    widgets->wifi_scanner = wifi_scanner_new(on_wifi_scan_results, widgets);
    widgets->bt_scanner = bluetooth_scanner_new(on_bt_scan_results, widgets);
    widgets->system_monitor = system_monitor_new(on_system_event, widgets);
    widgets->wifi_toggle_handler_id = g_signal_connect(widgets->wifi_toggle, "toggled", G_CALLBACK(on_expandable_toggle_toggled), widgets);
    widgets->bt_toggle_handler_id = g_signal_connect(widgets->bt_toggle, "toggled", G_CALLBACK(on_expandable_toggle_toggled), widgets);
    widgets->audio_toggle_handler_id = g_signal_connect(widgets->audio_toggle, "toggled", G_CALLBACK(on_expandable_toggle_toggled), widgets);
    g_signal_connect(airplane_toggle, "toggled", G_CALLBACK(toggle_airplane_mode), widgets);
    g_signal_connect_swapped(widgets->main_window, "destroy", G_CALLBACK(app_widgets_free), widgets);
    gboolean nm_ok = (g_object_get_data(G_OBJECT(app), "nm-init-failed") == NULL);

    if (nm_ok) {
        widgets->airplane_mode_active = is_airplane_mode_active();
        if (widgets->airplane_mode_active) {
            g_print("Airplane mode detected on startup.\n");
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(airplane_toggle), TRUE);
            gtk_widget_set_sensitive(widgets->wifi_toggle, FALSE);
            gtk_widget_set_sensitive(widgets->bt_toggle, FALSE);
        }
    } else {
        gtk_widget_set_sensitive(widgets->wifi_toggle, FALSE);
        gtk_widget_set_tooltip_text(widgets->wifi_toggle, "Could not connect to NetworkManager service.");
        gtk_widget_set_sensitive(airplane_toggle, FALSE);
        gtk_widget_set_tooltip_text(airplane_toggle, "NetworkManager service is unavailable.");
    }

    gtk_window_present(GTK_WINDOW(widgets->main_window));
    g_idle_add(initial_state_update, widgets);
}

static void load_or_reload_css() {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, CSS_PATH);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void on_css_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {
    (void)monitor; (void)file; (void)other_file; (void)user_data;
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
        g_print("Reloading CSS from: %s\n", CSS_PATH);
        load_or_reload_css();
    }
}

static void on_app_shutdown(GApplication *app, gpointer user_data) {
    (void)app; (void)user_data;
    network_manager_shutdown();
}

static void on_app_startup(GApplication *app, gpointer user_data) {
    (void)user_data;
    load_or_reload_css();
    GFile *css_file = g_file_new_for_path(CSS_PATH);
    GFileMonitor *monitor = g_file_monitor_file(css_file, G_FILE_MONITOR_NONE, NULL, NULL);
    g_object_set_data_full(G_OBJECT(app), "css-monitor", monitor, g_object_unref);
    g_signal_connect(monitor, "changed", G_CALLBACK(on_css_changed), NULL);
    g_object_unref(css_file);
    if (!network_manager_init()) {
        g_critical("Failed to initialize NetworkManager D-Bus connection. Wi-Fi functionality will be disabled.");
        g_object_set_data(G_OBJECT(app), "nm-init-failed", GINT_TO_POINTER(TRUE));
    }
}

int main(int argc, char **argv) {
    AdwApplication *app = adw_application_new("com.example.ControlCenter", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "startup", G_CALLBACK(on_app_startup), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_app_shutdown), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}