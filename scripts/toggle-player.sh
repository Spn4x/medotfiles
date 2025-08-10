#!/bin/bash

PLAYER_DIR="$HOME/.config/hypr/C-widgets/side-mpris-player/builddir"
PLAYER_NAME="./mpris-lyrics-viewer"

if pgrep -f "$PLAYER_NAME" > /dev/null; then
    pkill -f "$PLAYER_NAME"
else
    cd "$PLAYER_DIR" || exit
    ./mpris-lyrics-viewer &
fi
