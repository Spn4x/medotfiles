// src/control_center.c

#include "control_center.h"
#include "popout_panels.h"
#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>

// Fixed vertical position for each popout (pixels from top of window)
static const int POPUP_TOP_AUDIO = 290;
static const int POPUP_TOP_WIFI = 150;
static const int POPUP_TOP_BT   = 180;

// Horizontal adjustment: push farther right (pixels)
static const int POPUP_HORZ_ADJUST = -66;

// THE HARDCODED FIX: Define the width of the narrow control center sidebar.
static const int CONTROL_CENTER_SIDEBAR_WIDTH = 66;

// Track the currently open popout window and click controller
static GtkWidget *current_popout = NULL;
static GtkEventController *outside_click_controller = NULL;


// Forward declaration for the function that will attach the controller
static gboolean attach_click_outside_controller(gpointer user_data);


// Helper to clean up and destroy the current popout
static void destroy_current_popout(void) {
    if (!current_popout) {
        return;
    }
    gtk_window_destroy(GTK_WINDOW(current_popout));
    current_popout = NULL;
    if (outside_click_controller) {
        g_clear_object(&outside_click_controller);
    }
}

// Callback for clicks to close the popout
static void on_click_outside(GtkGestureClick *gesture,
                             int n_press,
                             double x,
                             double y,
                             gpointer user_data) {
    GtkWidget* popout_widget = GTK_WIDGET(user_data);
    GtkAllocation allocation;
    gtk_widget_get_allocation(popout_widget, &allocation);
    if (x >= 0 && y >= 0 && x < allocation.width && y < allocation.height) {
        return;
    }
    destroy_current_popout();
}


// Function to attach the click-outside controller
static gboolean attach_click_outside_controller(gpointer user_data) {
    if (!current_popout) {
        return G_SOURCE_REMOVE;
    }
    outside_click_controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
    gtk_event_controller_set_propagation_phase(outside_click_controller, GTK_PHASE_CAPTURE);
    g_signal_connect(outside_click_controller, "pressed", G_CALLBACK(on_click_outside), current_popout);
    gtk_widget_add_controller(current_popout, outside_click_controller);
    return G_SOURCE_REMOVE;
}


// Toggle a named popout, cleaning up any existing one
static void toggle_popout(GtkApplication *app, GtkWidget *trigger, const char *name) {
    // If a popout is already open...
    if (current_popout) {
        const char *open_name = g_object_get_data(G_OBJECT(current_popout), "popout-name");
        destroy_current_popout();
        // If the user clicked the same button again, we just close the popout and stop.
        if (g_strcmp0(open_name, name) == 0) {
            return;
        }
    }

    // Determine vertical top based on popout type
    int y_pos;
    if (g_strcmp0(name, "audio") == 0)      y_pos = POPUP_TOP_AUDIO;
    else if (g_strcmp0(name, "wifi") == 0)  y_pos = POPUP_TOP_WIFI;
    else if (g_strcmp0(name, "bluetooth") == 0) y_pos = POPUP_TOP_BT;
    else                                     y_pos = POPUP_TOP_AUDIO; // Default

    // Create the popout panel
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_ancestor(trigger, GTK_TYPE_WINDOW));
    GtkWidget *pop = NULL;
    if (g_strcmp0(name, "audio") == 0)
        pop = create_audio_panel(app, GTK_WIDGET(parent));
    else if (g_strcmp0(name, "wifi") == 0)
        pop = create_wifi_panel(app, GTK_WIDGET(parent));
    else if (g_strcmp0(name, "bluetooth") == 0)
        pop = create_bluetooth_panel(app, GTK_WIDGET(parent));
    
    if (!pop) return;

    g_object_set_data(G_OBJECT(pop), "popout-name", (gpointer)name);

    // --- THE HARDCODED FIX ---
    // We now use our simple, reliable, hardcoded width constant for positioning.
    // This removes all the complex and unpredictable dynamic measurement.
    gtk_layer_init_for_window(GTK_WINDOW(pop));
    gtk_layer_set_layer(GTK_WINDOW(pop), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(pop), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(pop), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    
    // Use our constant for the left margin.
    gtk_layer_set_margin(GTK_WINDOW(pop), GTK_LAYER_SHELL_EDGE_LEFT, CONTROL_CENTER_SIDEBAR_WIDTH + POPUP_HORZ_ADJUST);
    gtk_layer_set_margin(GTK_WINDOW(pop), GTK_LAYER_SHELL_EDGE_TOP, y_pos);
    // --- END OF FIX ---

    gtk_window_present(GTK_WINDOW(pop));
    current_popout = pop;

    g_idle_add(attach_click_outside_controller, NULL);
}

// Button signal handlers
static void on_audio_icon_clicked(GtkButton *button, gpointer user_data) {
    toggle_popout(GTK_APPLICATION(user_data), GTK_WIDGET(button), "audio");
}
static void on_wifi_icon_clicked(GtkButton *button, gpointer user_data) {
    toggle_popout(GTK_APPLICATION(user_data), GTK_WIDGET(button), "wifi");
}
static void on_bluetooth_icon_clicked(GtkButton *button, gpointer user_data) {
    toggle_popout(GTK_APPLICATION(user_data), GTK_WIDGET(button), "bluetooth");
}
static void on_back_button_clicked(GtkButton *button, gpointer user_data) {
    destroy_current_popout();
    gtk_stack_set_visible_child_name(GTK_STACK(user_data), "dock");
}

// Build the control center view
GtkWidget* create_control_center_view(GtkApplication *app, GtkStack *stack) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_margin_top(box, 10);

    GtkWidget *back_button = gtk_button_new_from_icon_name("go-previous-symbolic");
    g_signal_connect(back_button, "clicked", G_CALLBACK(on_back_button_clicked), stack);
    gtk_box_append(GTK_BOX(box), back_button);

    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *audio_button = gtk_button_new_from_icon_name("audio-card-symbolic");
    g_signal_connect(audio_button, "clicked", G_CALLBACK(on_audio_icon_clicked), app);
    gtk_box_append(GTK_BOX(box), audio_button);

    GtkWidget *wifi_button = gtk_button_new_from_icon_name("network-wireless-symbolic");
    g_signal_connect(wifi_button, "clicked", G_CALLBACK(on_wifi_icon_clicked), app);
    gtk_box_append(GTK_BOX(box), wifi_button);

    GtkWidget *bt_button = gtk_button_new_from_icon_name("bluetooth-symbolic");
    g_signal_connect(bt_button, "clicked", G_CALLBACK(on_bluetooth_icon_clicked), app);
    gtk_box_append(GTK_BOX(box), bt_button);

    return box;
}