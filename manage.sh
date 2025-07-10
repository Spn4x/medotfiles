#!/usr/bin/env bash

# =========================================================================
#             DOTFILES MANAGEMENT SCRIPT (UPGRADED)
#
#  Moves specified configs from ~/.config AND ~/ into this dotfiles
#  repo and creates symbolic links to them.
# =========================================================================

# The directory where this script is, which is the root of your dotfiles repo.
DOTFILES_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# --- LIST OF CONFIGS TO TRACK ---

# Directories from ~/.config
CONFIG_DIR="$HOME/.config"
TO_TRACK_CONFIG=(
    "hypr"
    "ironbar"
    "qt5ct"
    "qt6ct"
    "swaync"
    "waybar"
    "fastfetch"
    "wofi"
    "kitty"
    "wallust"
    "btop"
)

# Files or Directories from ~/ (your home directory)
HOME_DIR="$HOME"
TO_TRACK_HOME=(
    "scripts"
    # You could add ".zshrc" or ".bashrc" here in the future!
)

# --- SCRIPT LOGIC ---
echo "Starting dotfiles management..."
echo "Dotfiles repo is at: $DOTFILES_DIR"
echo "-----------------------------------------------------"

# --- Process ~/.config items ---
echo ">>> Processing ~/.config directories..."
# Create the .config directory inside dotfiles if it doesn't exist
mkdir -p "$DOTFILES_DIR/.config"

for item in "${TO_TRACK_CONFIG[@]}"; do
    SOURCE_PATH="$CONFIG_DIR/$item"
    DEST_PATH="$DOTFILES_DIR/.config/$item"

    echo "Processing: $item"
    if [ -e "$SOURCE_PATH" ] && [ ! -L "$SOURCE_PATH" ]; then
        echo "  -> Found real config. Moving and linking..."
        mv "$SOURCE_PATH" "$DEST_PATH"
        ln -s "$DEST_PATH" "$SOURCE_PATH"
        echo "  -> Done."
    elif [ -L "$SOURCE_PATH" ]; then
        echo "  -> Already linked. Skipping."
    else
        echo "  -> WARNING: Config for '$item' not found at '$SOURCE_PATH'. Skipping."
    fi
done

# --- Process ~/ items ---
echo ""
echo ">>> Processing ~/ items..."
for item in "${TO_TRACK_HOME[@]}"; do
    SOURCE_PATH="$HOME_DIR/$item"
    DEST_PATH="$DOTFILES_DIR/$item"

    echo "Processing: $item"
    if [ -e "$SOURCE_PATH" ] && [ ! -L "$SOURCE_PATH" ]; then
        echo "  -> Found real item. Moving and linking..."
        mv "$SOURCE_PATH" "$DEST_PATH"
        ln -s "$DEST_PATH" "$SOURCE_PATH"
        echo "  -> Done."
    elif [ -L "$SOURCE_PATH" ]; then
        echo "  -> Already linked. Skipping."
    else
        echo "  -> WARNING: Item '$item' not found at '$SOURCE_PATH'. Skipping."
    fi
done

echo ""
echo "-----------------------------------------------------"
echo "Dotfiles management complete!"