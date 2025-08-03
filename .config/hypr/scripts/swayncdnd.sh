#!/bin/bash
# swaync-dnd-toggle.sh

# Get current DND state
STATE=$(swaync-client --get-dnd)

if [ "$STATE" = "true" ]; then
    swaync-client --toggle-dnd
    notify-send "DND Disabled" "Notifications are back on"
else
    swaync-client --toggle-dnd
    notify-send "DND Enabled" "No more distractions"
fi
