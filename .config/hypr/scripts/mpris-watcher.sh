#!/bin/bash

DEBUG=true

log() {
    if [ "$DEBUG" = true ]; then
        echo "$(date +'%Y-%m-%d %H:%M:%S') $1"
    fi
}

get_player() {
    dbus-send --session --dest=org.freedesktop.DBus \
        --type=method_call --print-reply /org/freedesktop/DBus \
        org.freedesktop.DBus.ListNames |
    grep org.mpris.MediaPlayer2 | awk -F\" '{print $2}' | head -n 1
}

# --- Main Logic ---
player=$(get_player)
if [ -z "$player" ]; then
    log "ERROR: No MPRIS player found."
    exit 1
fi

log "Watching player: $player"
last_track_id=""

dbus-monitor "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',sender='$player'" |
while IFS= read -r line; do
    if echo "$line" | grep -q "Metadata"; then
        # Extract current track ID
        current_track_id=$(gdbus call --session \
            --dest "$player" \
            --object-path /org/mpris/MediaPlayer2 \
            --method org.freedesktop.DBus.Properties.Get \
            org.mpris.MediaPlayer2.Player Metadata |
            grep -o "objectpath '[^']*" | awk '{print $2}')

        log "Detected metadata change, trackid: '$current_track_id'"

        # Ignore "NoTrack"
        if [[ "$current_track_id" == "/org/mpris/MediaPlayer2/TrackList/NoTrack" ]]; then
            log "Skipping NoTrack: '$current_track_id'"
            continue
        fi

        # Handle new track
        if [[ -n "$current_track_id" && "$current_track_id" != "$last_track_id" ]]; then
            log "New track detected (was '$last_track_id')"
            last_track_id="$current_track_id"

            # Reset position immediately
            playerctl position 0
            log "Position reset to 0"

            # Check position after 6s and force reset if needed
            (
                sleep 0.1
                pos=$(playerctl position 2>/dev/null)
                log "0.1s later: playerctl reports position = $pos"
                if (( $(echo "$pos > 2" | bc -l) )); then
                    log "Position drifted, forcing back to 0"
                    playerctl position 0
                fi
            ) &
        fi
    fi
done
