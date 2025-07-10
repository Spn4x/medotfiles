#!/bin/bash

# Define your desired "small manageable" size for when it becomes floating
TARGET_WIDTH=800  # Adjust to your preference
TARGET_HEIGHT=500 # Adjust to your preference

# Get the floating state of the active window
# hyprctl -j activewindow outputs JSON, jq '.floating' extracts the boolean value
is_floating=$(hyprctl -j activewindow | jq '.floating')

if [ "$is_floating" == "true" ]; then
    # If the window is already floating, tile it by toggling its floating state
    hyprctl dispatch togglefloating active
else
    # If the window is tiled:
    # 1. Make it floating (togglefloating will achieve this as it's currently tiled)
    hyprctl dispatch togglefloating active
    
    # 2. Resize it to the target size
    # hyprctl dispatch resizeactive exact "$TARGET_WIDTH" "$TARGET_HEIGHT"
    
    # 3. Center it
    hyprctl dispatch centerwindow active
fi
