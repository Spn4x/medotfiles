#ifndef WIFI_SCANNER_H
#define WIFI_SCANNER_H

#include <glib.h>
#include "network_manager.h"

// Callback function prototype: it will be called with a fresh list of networks.
// The receiver of this callback is responsible for freeing the GList.
typedef void (*WifiScanResultCallback)(GList *networks, gpointer user_data);

typedef struct _WifiScanner WifiScanner;

// Creates a new WifiScanner object.
WifiScanner* wifi_scanner_new(WifiScanResultCallback callback, gpointer user_data);

// Starts the periodic scanning.
void wifi_scanner_start(WifiScanner *scanner, guint interval_seconds);

// Stops the periodic scanning.
void wifi_scanner_stop(WifiScanner *scanner);

// Triggers an immediate, one-time scan.
void wifi_scanner_trigger_scan(WifiScanner *scanner);

// Frees the scanner object.
void wifi_scanner_free(WifiScanner *scanner);

#endif // WIFI_SCANNER_H