#!/bin/bash

# Config
STATE_FILE="/tmp/hydration_state"
MAX_LEVEL=6
BAR_SYMBOLS=( "▓" "▓" "▒" "▒" "░" "░" )
LABEL="O2"
LEFT_EDGE="▐"
RIGHT_EDGE="▌"

# Read or initialize state
[[ ! -f "$STATE_FILE" ]] && echo 0 > "$STATE_FILE"
LEVEL=$(<"$STATE_FILE")

# Handle refill
if [[ "$1" == "refill" ]]; then
  LEVEL=0
else
  LEVEL=$((LEVEL + 1))
  (( LEVEL > MAX_LEVEL )) && LEVEL=MAX_LEVEL
fi

# Save new state
echo "$LEVEL" > "$STATE_FILE"

# Generate bar
BAR=""
for ((i=0; i<MAX_LEVEL; i++)); do
  if (( i < MAX_LEVEL - LEVEL )); then
    BAR+="${BAR_SYMBOLS[i]}"
  else
    BAR+=" "
  fi
done

# Output
echo "$LABEL $LEFT_EDGE$BAR$RIGHT_EDGE"
