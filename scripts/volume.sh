#!/usr/bin/env bash

status=$(volumectl get)

if [[ "$status" =~ [Mm]uted ]]; then
    icon="󰝟"
    vol="0%"
else
    vol_num=$(echo "$status" | grep -oE '[0-9]+')
    vol=${vol_num:-0}%
    if (( vol_num > 66 )); then
        icon="󰕾"
    elif (( vol_num > 33 )); then
        icon="󰖀"
    else
        icon="󰕿"
    fi
fi

echo "$icon $vol"
