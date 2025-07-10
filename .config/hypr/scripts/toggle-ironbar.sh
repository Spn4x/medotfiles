#!/bin/bash

if pgrep -x "ironbar" > /dev/null || pgrep -x "nm-applet" > /dev/null; then
    echo "Killing existing ironbar and nm-applet..."
    pkill -x ironbar
    pkill -x nm-applet
    exit 0
fi

echo "Starting ironbar..."
ironbar &

# Give ironbar a moment to initialize the tray
sleep 1

echo "Starting nm-applet..."
env XDG_CURRENT_DESKTOP=Unity nm-applet &
