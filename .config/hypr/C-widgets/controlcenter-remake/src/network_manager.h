// ===== src/network_manager.h =====
#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <glib.h>

// --- Init and Shutdown ---
gboolean network_manager_init();
void network_manager_shutdown();

typedef void (*NetworkOperationCallback)(gboolean success, gpointer user_data);

typedef struct {
    gchar *ssid;
    gchar *object_path; // D-Bus object path of the Access Point
    guint8 strength;
    gboolean is_secure;
    gboolean is_active;
} WifiNetwork;

// --- Radio Control ---
gboolean is_wifi_enabled();
void set_wifi_enabled_async(gboolean enabled, NetworkOperationCallback callback, gpointer user_data);
gboolean is_airplane_mode_active();

// --- Network Operations ---
GList* get_available_wifi_networks();

// Checks if a saved connection profile exists for a given SSID.
// The caller is responsible for freeing the returned string. Returns NULL if not found.
gchar* find_connection_for_ssid(const gchar *ssid);

// Asynchronously activates a known, existing connection.
void activate_wifi_connection_async(const gchar *connection_path,
                                    const gchar *ap_path,
                                    NetworkOperationCallback callback,
                                    gpointer user_data);

// Asynchronously adds and activates a NEW Wi-Fi network (for when no profile exists).
void add_and_activate_wifi_connection_async(const gchar *ssid,
                                            const gchar *ap_path,
                                            const gchar *password, // Can be NULL
                                            gboolean is_secure,
                                            NetworkOperationCallback callback,
                                            gpointer user_data);
                                   
void forget_wifi_connection_async(const gchar *ssid,
                                  NetworkOperationCallback callback,
                                  gpointer user_data);

void disconnect_wifi_async(NetworkOperationCallback callback,
                           gpointer user_data);

// --- Memory Management ---
void wifi_network_free(gpointer data);
void free_wifi_network_list(GList *list);

#endif // NETWORK_MANAGER_H