#!/usr/bin/env bash
set -euo pipefail

# 1) Locate Hyprland's event socket (.socket2.sock)
# We use HYPRLAND_INSTANCE_SIGNATURE if it's set, otherwise we find the socket.
if [[ -n "${HYPRLAND_INSTANCE_SIGNATURE-}" ]]; then
    SOCKET="/run/user/$(id -u)/hypr/${HYPRLAND_INSTANCE_SIGNATURE}/.socket2.sock"
else
    SOCKET=$(find "/run/user/$(id -u)/hypr" -type s -name ".socket2.sock" | head -n1)
fi

[[ -S "$SOCKET" ]] || { echo "❌ Cannot find Hyprland event socket at $SOCKET"; exit 1; }

# 2) Ensure Hyprland is running
pgrep -x Hyprland >/dev/null || { echo "❌ Hyprland not running"; exit 1; }

# 3) In-memory associative array: address → "class — title"
declare -A windows

# 4) Bootstrap from hyprctl to get the initial list of windows
while read -r entry; do
  addr=$(jq -r '.address'   <<<"$entry")
  cls=$(jq -r '.class'     <<<"$entry")
  ttl=$(jq -r '.title'     <<<"$entry")
  # Only add mapped (visible) windows to the initial list
  if jq -e '.mapped == true' <<<"$entry" >/dev/null; then
    windows["$addr"]="$cls — $ttl"
  fi
done < <(hyprctl -j clients | jq -c '.[]')

# 5) Function to redraw the list
draw() {
  clear
  echo "🖥️  Open apps @ $(date +'%H:%M:%S'):"
  # Check if the array is empty
  if [ ${#windows[@]} -eq 0 ]; then
    echo "• No open windows"
  else
    for addr in "${!windows[@]}"; do
      echo "• ${windows[$addr]}"
    done
  fi
  echo "──────────────────────────"
}

# Initial draw
draw

# 6) Listen for events and update the list in a single process
# By using < <(socat ...), the `while` loop runs in the current shell,
# allowing it to modify the main 'windows' array directly.
while IFS= read -r event; do
    event_type=${event%%>>*}
    event_data=${event#*>>}

    case "$event_type" in
        openwindow)
            # Event format: openwindow>>address,workspace,class,title
            IFS=',' read -r addr workspace cls ttl <<< "$event_data"
            # Sometimes title can be empty on open, we can live with it or re-query later
            windows["$addr"]="$cls — $ttl"
            draw
            ;;
        closewindow)
            # Event format: closewindow>>address
            addr="$event_data"
            # Check if the key exists before unsetting
            if [[ -v "windows[$addr]" ]]; then
                unset "windows[$addr]"
                draw
            fi
            ;;
        windowtitle)
            # Event format: windowtitle>>address
            addr="$event_data"
            # When a title changes, we must re-query hyprctl for the new info
            if [[ -v "windows[$addr]" ]]; then
                info=$(hyprctl -j clients | jq -r --arg a "$addr" '.[] | select(.address==$a) | "\(.class) — \(.title)"')
                windows["$addr"]="$info"
                draw
            fi
            ;;
    esac
done < <(socat -u "UNIX-CONNECT:$SOCKET" -)