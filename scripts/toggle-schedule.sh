#!/bin/bash

SCHEDULE_DIR="$HOME/.config/hypr/C-widgets/schedule-widget"
SCHEDULE_NAME="./build/gtk-schedule"

if pgrep -f "$SCHEDULE_NAME" > /dev/null; then
    pkill -f "$SCHEDULE_NAME"
else
    cd "$SCHEDULE_DIR" || exit
    ./build/gtk-schedule &
fi
