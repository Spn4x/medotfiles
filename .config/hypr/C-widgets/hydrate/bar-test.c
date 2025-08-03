#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, 20);
    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, 20);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(window), -1);  // Force it to draw OVER everything

    GtkWidget *label = gtk_label_new("🌊 hydrate me daddy");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(window), box);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

    GdkRGBA bg = {0.2, 0.2, 1.0, 0.8};  // semi-transparent blue
    gtk_widget_override_background_color(box, GTK_STATE_FLAG_NORMAL, &bg);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    g_print("[hydrate-test] Showing window...\n");
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
