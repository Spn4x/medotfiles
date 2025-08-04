#!/bin/bash

# REVISED - No icons/emojis

player_status=$(playerctl status 2>/dev/null)

if [ "$player_status" = "Playing" ]; then
    artist=$(playerctl metadata artist)
    title=$(playerctl metadata title)
    echo "[Playing] $artist - $title"
elif [ "$player_status" = "Paused" ]; then
    artist=$(playerctl metadata artist)
    title=$(playerctl metadata title)
    echo "[Paused] $artist - $title"
fi