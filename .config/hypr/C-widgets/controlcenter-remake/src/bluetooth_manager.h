#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <glib.h>

// Callback prototype for async operations.
typedef void (*BluetoothOperationCallback)(gboolean success, gpointer user_data);

// A struct to hold information about a single Bluetooth device.
typedef struct {
    gchar *address; // MAC address
    gchar *name;
    gboolean is_connected;
} BluetoothDevice;

// --- NEW: Power Control ---
// Checks if the Bluetooth adapter is powered on.
gboolean is_bluetooth_powered();

// Asynchronously sets the Bluetooth power state (on/off).
void set_bluetooth_powered_async(gboolean powered, BluetoothOperationCallback callback, gpointer user_data);
// --- END NEW ---

// Gets a list of all paired/known and recently scanned Bluetooth devices.
GList* get_available_bluetooth_devices();

// Asynchronously attempts to connect to a Bluetooth device by its address.
void connect_to_bluetooth_device_async(const gchar *address,
                                       BluetoothOperationCallback callback,
                                       gpointer user_data);

// Asynchronously disconnects from a Bluetooth device by its address.
void disconnect_bluetooth_device_async(const gchar *address,
                                       BluetoothOperationCallback callback,
                                       gpointer user_data);

// Frees a single BluetoothDevice struct.
void bluetooth_device_free(gpointer data);

// Frees a GList of BluetoothDevice structs.
void free_bluetooth_device_list(GList *list);

#endif // BLUETOOTH_MANAGER_H