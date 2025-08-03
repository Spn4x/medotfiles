#!/bin/bash

SIDEBAR_DIR="$HOME/.config/hypr/C-widgets/sidebar/builddir"
SIDEBAR_NAME="hypr-sidebar"

if pgrep -x "$SIDEBAR_NAME" > /dev/null; then
    pkill -x "$SIDEBAR_NAME"
else
    cd "$SIDEBAR_DIR" || exit
    ./hypr-sidebar &
fi
