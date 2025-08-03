#!/bin/bash

LAUNCHER_DIR="$HOME/dotfiles/.config/hypr/C-widgets/archlauncher-c"
LAUNCHER_BIN="$LAUNCHER_DIR/my-launcher"
LAUNCHER_NAME="my-launcher"

if pgrep -x "$LAUNCHER_NAME" > /dev/null; then
    pkill -x "$LAUNCHER_NAME"
else
    cd "$LAUNCHER_DIR" || exit
    "$LAUNCHER_BIN" &
fi
