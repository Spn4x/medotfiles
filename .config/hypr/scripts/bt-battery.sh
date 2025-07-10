#!/usr/bin/env bash

# --- Final Version based on new requirements ---
# - BT Off/Disconnected -> " Disconnected"
# - BT Connected -> " <percentage>% <device_name>"

# --- Configuration ---
HEADPHONE_ICON=""

# --- Main Logic ---

# 1. Check if Bluetooth is powered on.
#    If not, print the disconnected message and exit immediately.
if ! bluetoothctl show | grep -q "Powered: yes"; then
    echo "$  Disconnected"
    exit 0
fi

# 2. If powered on, check if a device is connected.
#    Get the line of the first connected device.
DEVICE_LINE=$(bluetoothctl devices Connected | grep "^Device" | head -n1)

# 3. If no device line was found, print the disconnected message and exit.
if [ -z "$DEVICE_LINE" ]; then
    echo "$HEADPHONE_ICON  Disconnected"
    exit 0
fi

# 4. If we are here, a device IS connected. Extract its info.
BLU_CONNECTED_MAC=$(echo "$DEVICE_LINE" | awk '{print $2}')
# Use 'cut' to correctly get the full device name, even if it has spaces.
BLU_CONNECTED_NAME=$(echo "$DEVICE_LINE" | cut -d ' ' -f3-)

# 5. Get the battery level using the reliable pipe method.
DEV_BATTERY=$(echo -e "info $BLU_CONNECTED_MAC\nexit" | bluetoothctl | sed -n '/Battery Percentage:/ s/.*(\([0-9]*\).*/\1/p')

# 6. Format and print the final "Connected" output.
if [ -n "$DEV_BATTERY" ]; then
    # Battery percentage is available
    echo "$HEADPHONE_ICON  $DEV_BATTERY% $BLU_CONNECTED_NAME"
else
    # No battery percentage, just show the device name
    echo "$HEADPHONE_ICON  $BLU_CONNECTED_NAME"
fi