#!/bin/sh

# The path to the temporary file where we store wellbeing data
# Using /tmp is simple and accessible by both user and root
WELLBEING_DATA_FILE="/tmp/sigma_dashboard_wellbeing.txt"

# 1. Gather User-Level Data
# Run hypr-wellbeing as the current user and pipe the output to our temp file.
# The '|| true' ensures the script doesn't fail if hypr-wellbeing has a minor error.
echo "Gathering wellbeing data..."
hypr-wellbeing --show > "$WELLBEING_DATA_FILE" || true

# 2. Launch the Main UI as Root
# The pkexec command remains the same, launching our Python script with root privileges.
echo "Launching dashboard as root..."
SCRIPT_DIR=$(dirname "$(realpath "$0")")
pkexec env \
    GTK_THEME=Adwaita:dark \
    DISPLAY=$DISPLAY \
    WAYLAND_DISPLAY=$WAYLAND_DISPLAY \
    XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR \
    DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS \
    python "$SCRIPT_DIR/thinkfanui.py"
