#!/bin/bash

# ~/.config/hypr/scripts/volume_avizo.sh

# Configuration
VOLUME_STEP=5
LOCK_FILE="/tmp/volume_avizo.lock"
SCRIPT_LOG="/tmp/volume_avizo_script.log" # Optional: for your debugging

# Simple logging; you can remove this if you don't need it
echo "--- Volume Avizo Script ($1) started at $(date) by PID $$ ---" >> "$SCRIPT_LOG"

(
    if ! flock -n 200; then
        echo "Lock NOT acquired by PID $$. Exiting subshell." >> "$SCRIPT_LOG"
        exit 1
    fi
    # echo "Lock acquired by PID $$ for action '$1'" >> "$SCRIPT_LOG" # Verbose logging

    case "$1" in
        up)
            pamixer --unmute # Ensure unmuted before increasing
            pamixer -i "$VOLUME_STEP"
            # echo "Volume increased by $VOLUME_STEP%" >> "$SCRIPT_LOG"
            ;;
        down)
            # pamixer --unmute # Optional: unmute when decreasing
            pamixer -d "$VOLUME_STEP"
            # echo "Volume decreased by $VOLUME_STEP%" >> "$SCRIPT_LOG"
            ;;
        mute)
            pamixer -t # Toggle mute
            # echo "Mute toggled" >> "$SCRIPT_LOG"
            ;;
        get)
            # This action is just to trigger the OSD update without changing volume
            # echo "Get action: Forcing Avizo update." >> "$SCRIPT_LOG"
            ;;
        *)
            echo "Usage: $0 {up|down|mute|get}" >> "$SCRIPT_LOG"
            # echo "Invalid action: '$1'" >> "$SCRIPT_LOG"
            exit 1
            ;;
    esac

    # Trigger Avizo OSD update - SIMPLIFIED CALLS
    # Avizo should pick up the changes made by pamixer automatically for volume/mute.
    # We just need to tell it which type of notification to display.
    if pamixer --get-mute | grep -q "true"; then
        avizo-client mute # Just tell it it's a mute event
        # echo "Avizo triggered for mute status" >> "$SCRIPT_LOG"
    else
        avizo-client volume # Just tell it it's a volume event
        # echo "Avizo triggered for volume event" >> "$SCRIPT_LOG"
    fi

    # echo "Lock to be released by PID $$ for action '$1' (subshell exiting)" >> "$SCRIPT_LOG" # Verbose
    exit 0
) 200>"$LOCK_FILE"

SUBSHELL_EXIT_STATUS=$?
# if [ $SUBSHELL_EXIT_STATUS -ne 0 ]; then
#     echo "volume_avizo.sh: Subshell exited with status $SUBSHELL_EXIT_STATUS for action '$1'." >> "$SCRIPT_LOG"
# fi

# echo "--- Volume Avizo Script ($1) finished at $(date) by PID $$ ---" >> "$SCRIPT_LOG" # Verbose
exit $SUBSHELL_EXIT_STATUS