#ifndef UTILS_H
#define UTILS_H

#include <gio/gio.h>

void execute_command_async(const char* command, GAsyncReadyCallback callback, gpointer user_data);

// Declare the new functions to get specific output streams
const char* get_command_stdout(GAsyncResult *res);
const char* get_command_stderr(GAsyncResult *res);

#endif