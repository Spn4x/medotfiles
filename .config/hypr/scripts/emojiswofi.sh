
#!/bin/bash

# Path to your emoji list and CSS
EMOJI_FILE="$HOME/dotfiles/.config/hypr/scripts/emoji.txt"
STYLE="$HOME/.config/wofi/style-wallust-generated.css"

# Launch Wofi to pick an emoji
chosen=$(cat "$EMOJI_FILE" | wofi \
  --dmenu \
  --prompt "Emoji:" \
  --style "$STYLE" \
  --allow-images \
  --insensitive)

if [ -n "$chosen" ]; then
  emoji=$(echo "$chosen" | awk '{print $1}')
  echo -n "$emoji" | wl-copy
  notify-send "Copied!" "$emoji"
fi
