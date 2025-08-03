#ifndef CONTROL_CENTER_H
#define CONTROL_CENTER_H

#include <gtk/gtk.h>

// This struct is now defined in the header so it's visible to all files.
typedef struct {
    GtkWidget *volume_slider;
    GtkWidget *output_selector;
} ControlWidgets;

// This is the only function from control_center.c that other files need.
GtkWidget* create_control_center_view(GtkApplication *app, GtkStack *stack);

#endif