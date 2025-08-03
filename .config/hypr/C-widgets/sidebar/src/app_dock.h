// src/app_dock.h

#ifndef APP_DOCK_H
#define APP_DOCK_H

#include <gtk/gtk.h>

GtkWidget* create_dock_view(GtkStack *stack);
// The signature must match the implementation in the .c file for use with g_timeout_add
gboolean update_running_apps(gpointer user_data);

#endif