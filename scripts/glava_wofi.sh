#!/usr/bin/env bash

# Script to launch new Glava visualizer instances via wofi,
# or stop all existing instances.
# Uses Wallust-generated wofi theme if available.

# --- Configuration ---
WOFI_STYLE_PATH="$HOME/.config/wofi/style-wallust-generated.css"
options="Bars Mode (New Instance)\nRadial Mode (New Instance)\nStop ALL Glava Instances"
prompt="Select Glava Action:"

# --- Wofi Command Setup ---
wofi_command_args=("--dmenu" "--prompt" "$prompt")
if [ -f "$WOFI_STYLE_PATH" ]; then
    wofi_command_args+=("--style" "$WOFI_STYLE_PATH")
else
    echo "WARNING: Wallust-generated Wofi style not found at '$WOFI_STYLE_PATH'. Using default." >&2
fi

# --- Helper Function ---

# Kills all running glava instances
kill_all_glava_instances() {
    if pgrep -x "glava" > /dev/null; then
        echo "Stopping ALL existing Glava instance(s)..."
        pkill glava
        sleep 0.5 # Give it a moment to terminate gracefully
        return 0 # Indicate that instances were likely killed
    fi
    return 1 # Indicate no instances were running to kill
}

# --- Main Logic ---
chosen=$(echo -e "$options" | wofi "${wofi_command_args[@]}")

case "$chosen" in
    "Bars Mode (New Instance)")
        echo "Starting a new Glava instance in Bars Mode..."
        glava --force-mod bars &
        # notify-send "Glava" "New Bars Mode instance started." # Optional
        ;;

    "Radial Mode (New Instance)")
        echo "Starting a new Glava instance in Radial Mode..."
        glava --force-mod radial &
        # notify-send "Glava" "New Radial Mode instance started." # Optional
        ;;

    "Stop ALL Glava Instances")
        if kill_all_glava_instances; then
            echo "All Glava instances stopped."
            # notify-send "Glava" "All instances stopped." # Optional
        else
            echo "No Glava instances were running."
            # notify-send "Glava" "No instances were running." # Optional
        fi
        ;;

    *)
        if [ -z "$chosen" ]; then
            echo "No option selected. Exiting."
        else
            echo "Invalid choice: '$chosen'. Exiting."
        fi
        exit 1
        ;;
esac

exit 0
