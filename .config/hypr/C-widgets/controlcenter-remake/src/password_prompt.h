#ifndef PASSWORD_PROMPT_H
#define PASSWORD_PROMPT_H

#include <gtk/gtk.h>
#include "gtk4-layer-shell.h"

// Callback for when the user submits the password.
// The `password` is the text from the entry, or NULL if cancelled.
typedef void (*PasswordSubmitCallback)(const gchar *ssid, const gchar *password, gpointer user_data);

// Shows a modal dialog to ask for the password for a given SSID.
void prompt_for_wifi_password(GtkWindow *parent,
                              const gchar *ssid,
                              PasswordSubmitCallback callback,
                              gpointer user_data);

#endif // PASSWORD_PROMPT_H