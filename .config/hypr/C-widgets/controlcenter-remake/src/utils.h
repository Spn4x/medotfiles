#ifndef UTILS_H
#define UTILS_H

#include <glib.h>

// Runs a command and returns its standard output as a newly allocated string.
// The caller is responsible for freeing the returned string with g_free().
// Returns NULL on error.
gchar* run_command(const char *command_line);

#endif // UTILS_H