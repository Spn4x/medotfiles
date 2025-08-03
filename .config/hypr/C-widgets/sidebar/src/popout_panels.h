// src/popout_panels.h

#ifndef POPOUT_PANELS_H
#define POPOUT_PANELS_H

#include <gtk/gtk.h>

// CORRECTED: Removed the top_margin parameter from all creation functions.
GtkWidget* create_audio_panel(GtkApplication *app, GtkWidget *parent_window);
GtkWidget* create_wifi_panel(GtkApplication *app, GtkWidget *parent_window);
GtkWidget* create_bluetooth_panel(GtkApplication *app, GtkWidget *parent_window);

#endif