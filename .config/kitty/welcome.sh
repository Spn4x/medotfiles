#!/bin/bash

clear # Clear the screen first

# --- Configuration ---
GIF_PATH="/home/spn4x/Downloads/adventureroomprev.gif" # Make sure this path is correct!

# Estimate image dimensions in character cells for positioning text
# Adjust these based on how the image actually renders in your terminal
ESTIMATED_IMG_WIDTH=22  # Reduced from 25
ESTIMATED_IMG_HEIGHT=12 # How many rows high does the image look?

# Positioning for the text block
TEXT_PADDING=2       # Reduced from 4
TEXT_START_ROW=1     # Start text on which row (1 = top row, 2 = second row, etc.)? Adjust for vertical alignment.
TEXT_START_COL=$((ESTIMATED_IMG_WIDTH + TEXT_PADDING)) # Calculate starting column based on image width and padding

# --- Display Animated GIF ---
cat "$GIF_PATH" | kitty +icat --align left &
sleep 0.1

# --- Gather System Info (Reduced) ---
os_info=$(grep PRETTY_NAME /etc/os-release | cut -d'=' -f2 | tr -d '"' || echo "Unknown OS")
kernel_info=$(uname -r || echo "Unknown Kernel")
uptime_info=$(uptime -p | sed 's/up //' || echo "Unknown Uptime")

# --- Display System Info using tput (Reduced) ---
GREEN='\e[32m'
RESET='\e[0m'
CURRENT_ROW=$TEXT_START_ROW

# Function to place cursor and print
print_at() {
    local row=$1
    local col=$2
    shift 2
    if [[ "$row" =~ ^[0-9]+$ && "$col" =~ ^[0-9]+$ ]]; then
        tput cup "$row" "$col"
        printf "$@"
    else
        printf "$@"
    fi
}

# Print only the desired lines
print_at $CURRENT_ROW $TEXT_START_COL "${GREEN}${RESET}  %s\n" "$os_info"
((CURRENT_ROW++))

print_at $CURRENT_ROW $TEXT_START_COL "${GREEN}${RESET}  %s\n" "$kernel_info"
((CURRENT_ROW++))

print_at $CURRENT_ROW $TEXT_START_COL "${GREEN}${RESET}  %s\n" "$uptime_info"
((CURRENT_ROW++))

# Move cursor below the estimated image height AND below the text block
FINAL_ROW=$(( (ESTIMATED_IMG_HEIGHT > CURRENT_ROW ? ESTIMATED_IMG_HEIGHT : CURRENT_ROW) + 1 ))
if [[ "$FINAL_ROW" =~ ^[0-9]+$ ]]; then
    tput cup $FINAL_ROW 0
else
    echo # Fallback
fi

wait