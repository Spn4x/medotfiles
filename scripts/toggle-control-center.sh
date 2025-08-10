#!/bin/bash

SIDEBAR_DIR="$HOME/.config/hypr/C-widgets/controlcenter-remake"
SIDEBAR_NAME="./build/control-center"

if pgrep -f "$SIDEBAR_NAME" > /dev/null; then
    pkill -f "$SIDEBAR_NAME"
else
    cd "$SIDEBAR_DIR" || exit
    ./build/control-center &
fi
