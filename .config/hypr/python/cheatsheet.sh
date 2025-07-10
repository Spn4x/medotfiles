#!/bin/bash

# Path to the script
SCRIPT_PATH="/home/meismeric/.config/hypr/python/cheatsheet.py"

# Check if it's already running
if pgrep -f "$SCRIPT_PATH" > /dev/null; then
    # Kill it
    pkill -f "$SCRIPT_PATH"
else
    # Launch it in background
    python "$SCRIPT_PATH" &
fi
