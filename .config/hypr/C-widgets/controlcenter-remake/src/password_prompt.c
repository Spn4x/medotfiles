// ===== src/password_prompt.c =====
#include "password_prompt.h"

typedef struct {
    gchar *ssid;
    PasswordSubmitCallback callback;
    gpointer user_data;
    GtkWindow *prompt_window;
    GtkWidget *password_entry;
} PromptData;

// This function is now called when the user clicks a button
static void on_button_clicked(GtkButton* button, PromptData *data) {
    const char *label = gtk_button_get_label(button);

    if (g_strcmp0(label, "Connect") == 0) {
        const char *password = gtk_editable_get_text(GTK_EDITABLE(data->password_entry));
        data->callback(data->ssid, password, data->user_data);
    } else { // Cancel
        data->callback(data->ssid, NULL, data->user_data);
    }
    
    gtk_window_destroy(GTK_WINDOW(data->prompt_window));
}

// This function is called when the window is destroyed (e.g., by our code or user hitting Esc)
static void on_prompt_destroyed(GtkWidget *widget, PromptData *data) {
    (void)widget;
    g_free(data->ssid);
    g_free(data);
}

void prompt_for_wifi_password(GtkWindow *parent,
                              const gchar *ssid,
                              PasswordSubmitCallback callback,
                              gpointer user_data)
{
    // Create the top-level window
    GtkWidget *window = gtk_application_window_new(gtk_window_get_application(parent));
    gtk_window_set_transient_for(GTK_WINDOW(window), parent);

    // Make it a layer shell surface
    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_namespace(GTK_WINDOW(window), "Password Prompt");
    // Anchor to all edges to create an overlay
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    // Allow user to dismiss by pressing Esc
    gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    // Create an overlay to dim the background
    GtkWidget *overlay = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(overlay, "password-prompt-overlay");
    gtk_widget_set_halign(overlay, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(overlay, GTK_ALIGN_CENTER);
    gtk_window_set_child(GTK_WINDOW(window), overlay);

    // Create a box for the content
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(vbox, "main-popup"); // Re-use the CSS style
    gtk_widget_set_size_request(vbox, 340, -1);
    gtk_box_append(GTK_BOX(overlay), vbox);

    // Add widgets to the box
    gchar *title_text = g_strdup_printf("Enter password for <b>%s</b>", ssid);
    GtkWidget *title_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_label), title_text);
    g_free(title_text);
    gtk_widget_set_margin_top(title_label, 12);
    gtk_widget_set_margin_start(title_label, 12);
    gtk_widget_set_margin_end(title_label, 12);

    GtkWidget *password_entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(password_entry), TRUE);
    gtk_widget_set_margin_start(password_entry, 12);
    gtk_widget_set_margin_end(password_entry, 12);
    gtk_widget_set_valign(password_entry, GTK_ALIGN_CENTER);
    // Grab focus so user can type immediately
    gtk_widget_grab_focus(password_entry);

    // Box for buttons
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(button_box, 6);
    gtk_widget_set_margin_bottom(button_box, 12);
    gtk_widget_set_margin_start(button_box, 12);
    gtk_widget_set_margin_end(button_box, 12);

    GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
    GtkWidget *connect_button = gtk_button_new_with_label("Connect");
    gtk_widget_add_css_class(connect_button, "suggested-action");

    gtk_box_append(GTK_BOX(button_box), cancel_button);
    gtk_box_append(GTK_BOX(button_box), connect_button);

    gtk_box_append(GTK_BOX(vbox), title_label);
    gtk_box_append(GTK_BOX(vbox), password_entry);
    gtk_box_append(GTK_BOX(vbox), button_box);

    PromptData *data = g_new0(PromptData, 1);
    data->ssid = g_strdup(ssid);
    data->callback = callback;
    data->user_data = user_data;
    data->password_entry = password_entry;
    data->prompt_window = GTK_WINDOW(window);

    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_button_clicked), data);
    g_signal_connect(connect_button, "clicked", G_CALLBACK(on_button_clicked), data);
    // Connect to destroy to free our data struct
    g_signal_connect(window, "destroy", G_CALLBACK(on_prompt_destroyed), data);
    
    gtk_window_present(GTK_WINDOW(window));
}