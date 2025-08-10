#ifndef BLUETOOTH_SCANNER_H
#define BLUETOOTH_SCANNER_H

#include <glib.h>
#include "bluetooth_manager.h"

// Callback function prototype: it will be called with a fresh list of devices.
// The receiver of this callback is responsible for freeing the GList.
typedef void (*BluetoothScanResultCallback)(GList *devices, gpointer user_data);

typedef struct _BluetoothScanner BluetoothScanner;

// Creates a new BluetoothScanner object.
BluetoothScanner* bluetooth_scanner_new(BluetoothScanResultCallback callback, gpointer user_data);

// Starts the periodic scanning.
void bluetooth_scanner_start(BluetoothScanner *scanner, guint interval_seconds);

// Stops the periodic scanning.
void bluetooth_scanner_stop(BluetoothScanner *scanner);

// Triggers an immediate, one-time scan.
void bluetooth_scanner_trigger_scan(BluetoothScanner *scanner);

// Frees the scanner object.
void bluetooth_scanner_free(BluetoothScanner *scanner);

#endif // BLUETOOTH_SCANNER_H