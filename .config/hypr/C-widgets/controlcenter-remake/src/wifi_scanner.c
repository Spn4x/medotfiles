#include "wifi_scanner.h"

struct _WifiScanner {
    guint timer_id;
    WifiScanResultCallback callback;
    gpointer user_data;
};

// This is the function that gets called by the timer.
static gboolean on_scan_timer_tick(gpointer user_data) {
    WifiScanner *scanner = (WifiScanner*)user_data;
    
    // Perform the scan and call the main callback.
    wifi_scanner_trigger_scan(scanner);

    // Return G_SOURCE_CONTINUE to keep the timer running.
    return G_SOURCE_CONTINUE;
}

WifiScanner* wifi_scanner_new(WifiScanResultCallback callback, gpointer user_data) {
    WifiScanner *scanner = g_new0(WifiScanner, 1);
    scanner->callback = callback;
    scanner->user_data = user_data;
    return scanner;
}

void wifi_scanner_start(WifiScanner *scanner, guint interval_seconds) {
    if (scanner->timer_id > 0) {
        // Already running
        return;
    }
    // Run the timer every `interval_seconds`.
    scanner->timer_id = g_timeout_add_seconds(interval_seconds, on_scan_timer_tick, scanner);
    
    // Trigger one scan immediately on start so the UI isn't empty.
    wifi_scanner_trigger_scan(scanner);
}

void wifi_scanner_stop(WifiScanner *scanner) {
    if (scanner->timer_id > 0) {
        g_source_remove(scanner->timer_id);
        scanner->timer_id = 0;
    }
}

void wifi_scanner_trigger_scan(WifiScanner *scanner) {
    if (!scanner->callback) {
        return;
    }
    g_print("Scanning for Wi-Fi networks...\n");
    GList *networks = get_available_wifi_networks();
    scanner->callback(networks, scanner->user_data);
}

void wifi_scanner_free(WifiScanner *scanner) {
    wifi_scanner_stop(scanner);
    g_free(scanner);
}