// src/utils.h

#ifndef UTILS_H
#define UTILS_H

#include <gio/gio.h>

void execute_command_async(const char* command, GAsyncReadyCallback callback, gpointer user_data);

// We only need stdout. The caller of this function receives ownership of the returned string.
gchar* get_command_stdout(GAsyncResult *res);

#endif