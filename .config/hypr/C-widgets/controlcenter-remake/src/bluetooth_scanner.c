// ===== src/bluetooth_scanner.c =====
#include "bluetooth_scanner.h"
#include "utils.h"

struct _BluetoothScanner {
    guint timer_id;
    BluetoothScanResultCallback callback;
    gpointer user_data;
};

// This is the function that gets called by the timer.
static gboolean on_scan_timer_tick(gpointer user_data) {
    BluetoothScanner *scanner = (BluetoothScanner*)user_data;
    
    // Perform the scan and call the main callback.
    bluetooth_scanner_trigger_scan(scanner);

    // Return G_SOURCE_CONTINUE to keep the timer running.
    return G_SOURCE_CONTINUE;
}

BluetoothScanner* bluetooth_scanner_new(BluetoothScanResultCallback callback, gpointer user_data) {
    BluetoothScanner *scanner = g_new0(BluetoothScanner, 1);
    scanner->callback = callback;
    scanner->user_data = user_data;
    return scanner;
}

void bluetooth_scanner_start(BluetoothScanner *scanner, guint interval_seconds) {
    if (scanner->timer_id > 0) {
        // Already running
        return;
    }
    // Run bluetoothctl scan on to ensure we see new devices
    run_command("bluetoothctl scan on &");

    // Run the timer every `interval_seconds`.
    scanner->timer_id = g_timeout_add_seconds(interval_seconds, on_scan_timer_tick, scanner);
    
    // Trigger one scan immediately on start so the UI isn't empty.
    bluetooth_scanner_trigger_scan(scanner);
}

void bluetooth_scanner_stop(BluetoothScanner *scanner) {
    if (scanner->timer_id > 0) {
        g_source_remove(scanner->timer_id);
        scanner->timer_id = 0;
    }
    // It's good practice to turn scanning off to save power.
    run_command("bluetoothctl scan off");
}

void bluetooth_scanner_trigger_scan(BluetoothScanner *scanner) {
    if (!scanner->callback) {
        return;
    }
    g_print("Scanning for Bluetooth devices...\n");
    GList *devices = get_available_bluetooth_devices();
    scanner->callback(devices, scanner->user_data);
}

void bluetooth_scanner_free(BluetoothScanner *scanner) {
    bluetooth_scanner_stop(scanner);
    g_free(scanner);
}