// ===== src/bluetooth_manager.c =====
#include "bluetooth_manager.h"
#include "utils.h"
#include <gio/gio.h>
#include <string.h>

// --- Data structures for async operations ---
typedef struct {
    gchar *address;
    gboolean powered; // Used for power on/off
} BtOperationTaskData;

typedef struct {
    BluetoothOperationCallback user_callback;
    gpointer user_data;
} BtOperationFinishData;

static void bt_operation_task_data_free(gpointer data) {
    BtOperationTaskData *task_data = data;
    if (!task_data) return;
    g_free(task_data->address);
    g_free(task_data);
}


// --- FORWARD DECLARATION ---
static void on_bt_operation_finished(GObject *source_object, GAsyncResult *res, gpointer user_data);
// --- END FORWARD DECLARATION ---


// --- Power Control Implementation ---
gboolean is_bluetooth_powered() {
    g_autofree gchar *output = run_command("bluetoothctl show");
    if (!output) return FALSE;
    return (strstr(output, "Powered: yes") != NULL);
}

static void set_powered_task_thread_func(GTask *task, gpointer s, gpointer d, GCancellable *c) {
    (void)s; (void)c; (void)d;
    BtOperationTaskData *data = g_task_get_task_data(task);
    const char *state = data->powered ? "on" : "off";
    gchar *cmd = g_strdup_printf("bluetoothctl power %s", state);
    gint exit_status;
    g_spawn_command_line_sync(cmd, NULL, NULL, &exit_status, NULL);
    g_free(cmd);
    g_task_return_boolean(task, exit_status == 0);
}

void set_bluetooth_powered_async(gboolean powered, BluetoothOperationCallback cb, gpointer ud) {
    BtOperationTaskData *task_data = g_new0(BtOperationTaskData, 1);
    task_data->powered = powered;
    task_data->address = NULL;
    BtOperationFinishData *finish_data = g_new0(BtOperationFinishData, 1);
    finish_data->user_callback = cb;
    finish_data->user_data = ud;
    GTask *task = g_task_new(NULL, NULL, on_bt_operation_finished, finish_data);
    g_task_set_task_data(task, task_data, bt_operation_task_data_free);
    g_task_run_in_thread(task, set_powered_task_thread_func);
    g_object_unref(task);
}

// --- Generic callback for all async operations ---
static void on_bt_operation_finished(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    (void)source_object;
    BtOperationFinishData *finish_data = user_data;
    gboolean success = g_task_propagate_boolean(G_TASK(res), NULL);
    if (finish_data->user_callback) {
        finish_data->user_callback(success, finish_data->user_data);
    }
    g_free(finish_data);
}

// --- Connect Logic ---
static void connect_task_thread_func(GTask *task, gpointer s, gpointer d, GCancellable *c) {
    (void)s; (void)c; (void)d;
    BtOperationTaskData *data = g_task_get_task_data(task);
    gchar *cmd = g_strdup_printf("bluetoothctl connect %s", data->address);
    gint exit_status;
    g_spawn_command_line_sync(cmd, NULL, NULL, &exit_status, NULL);
    g_free(cmd);
    g_task_return_boolean(task, exit_status == 0);
}

void connect_to_bluetooth_device_async(const gchar *address, BluetoothOperationCallback cb, gpointer ud) {
    BtOperationTaskData *task_data = g_new0(BtOperationTaskData, 1);
    task_data->address = g_strdup(address);
    BtOperationFinishData *finish_data = g_new0(BtOperationFinishData, 1);
    finish_data->user_callback = cb;
    finish_data->user_data = ud;
    GTask *task = g_task_new(NULL, NULL, on_bt_operation_finished, finish_data);
    g_task_set_task_data(task, task_data, bt_operation_task_data_free);
    g_task_run_in_thread(task, connect_task_thread_func);
    g_object_unref(task);
}

// --- Disconnect Logic ---
static void disconnect_task_thread_func(GTask *task, gpointer s, gpointer d, GCancellable *c) {
    (void)s; (void)c; (void)d;
    BtOperationTaskData *data = g_task_get_task_data(task);
    gchar *cmd = g_strdup_printf("bluetoothctl disconnect %s", data->address);
    gint exit_status;
    g_spawn_command_line_sync(cmd, NULL, NULL, &exit_status, NULL);
    g_free(cmd);
    g_task_return_boolean(task, exit_status == 0);
}

void disconnect_bluetooth_device_async(const gchar *address, BluetoothOperationCallback cb, gpointer ud) {
    BtOperationTaskData *task_data = g_new0(BtOperationTaskData, 1);
    task_data->address = g_strdup(address);
    BtOperationFinishData *finish_data = g_new0(BtOperationFinishData, 1);
    finish_data->user_callback = cb;
    finish_data->user_data = ud;
    GTask *task = g_task_new(NULL, NULL, on_bt_operation_finished, finish_data);
    g_task_set_task_data(task, task_data, bt_operation_task_data_free);
    g_task_run_in_thread(task, disconnect_task_thread_func);
    g_object_unref(task);
}

// --- Device Discovery Logic ---
static gint sort_devices(gconstpointer a, gconstpointer b) {
    const BluetoothDevice *dev_a = a; const BluetoothDevice *dev_b = b;
    if (dev_a->is_connected && !dev_b->is_connected) return -1;
    if (!dev_a->is_connected && dev_b->is_connected) return 1;
    return g_strcmp0(dev_a->name, dev_b->name);
}

GList* get_available_bluetooth_devices() {
    g_autofree gchar *devices_output = run_command("bluetoothctl devices");
    if (!devices_output) return NULL;

    GHashTable *connected_devices = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_autofree gchar *connected_output = run_command("bluetoothctl devices Connected");
    if (connected_output) {
        gchar **lines = g_strsplit(connected_output, "\n", -1);
        for (int i = 0; lines[i] != NULL && lines[i][0] != '\0'; i++) {
            gchar **fields = g_strsplit(lines[i], " ", 3);
            if (g_strv_length(fields) > 1) {
                g_hash_table_insert(connected_devices, g_strdup(fields[1]), GINT_TO_POINTER(1));
            }
            g_strfreev(fields);
        }
        g_strfreev(lines);
        // FIX: REMOVE the manual free. g_autofree will handle it.
        // g_free(connected_output); 
    }

    GList *devices = NULL;
    gchar **lines = g_strsplit(devices_output, "\n", -1);
    for (int i = 0; lines[i] != NULL && lines[i][0] != '\0'; i++) {
        if (!g_str_has_prefix(lines[i], "Device ")) {
            continue;
        }

        gchar **fields = g_strsplit(lines[i], " ", 3);
        if (g_strv_length(fields) == 3) {
            BluetoothDevice *dev = g_new0(BluetoothDevice, 1);
            dev->address = g_strdup(fields[1]);
            dev->name = g_strdup(fields[2]);
            dev->is_connected = g_hash_table_contains(connected_devices, dev->address);
            devices = g_list_append(devices, dev);
        }
        g_strfreev(fields);
    }
    g_strfreev(lines);
    // FIX: REMOVE the manual free. g_autofree will handle it.
    // g_free(devices_output);

    g_hash_table_destroy(connected_devices);

    devices = g_list_sort(devices, sort_devices);

    return devices;
}


// --- Utility Functions ---
void bluetooth_device_free(gpointer data) {
    if (!data) return;
    BluetoothDevice *dev = (BluetoothDevice*)data;
    g_free(dev->address);
    g_free(dev->name);
    g_free(dev);
}

void free_bluetooth_device_list(GList *list) {
    g_list_free_full(list, bluetooth_device_free);
}