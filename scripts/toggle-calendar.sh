#!/bin/bash

    CALENDAR_DIR="$HOME/.config/hypr/C-widgets/hyper-calendar/"
CALENDAR_NAME="./build/sleek-calendar"

if pgrep -f "$CALENDAR_NAME" > /dev/null; then
    pkill -f "$CALENDAR_NAME"
else
    cd "$CALENDAR_DIR" || exit
    ./build/sleek-calendar &
fi
