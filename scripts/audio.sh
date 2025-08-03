#!/bin/bash

# Emoji for outputs
get_icon() {
    case "$1" in
        *soundcore*|*Earphones*|*earphone*|*R50i*) echo "🎧";;
        *Built-in*|*Speaker*|*Analog*) echo "🔊";;
        *) echo "🌀";;
    esac
}

# Get sinks: skip EasyEffects and junk
get_sinks() {
    wpctl status | awk '/Sinks:/, /Sources:/' |
        grep -v 'Easy Effects Sink' |
        grep -E '[0-9]+\.' |
        sed 's/[*│]//g' |
        sed -E 's/^[[:space:]]*([0-9]+)\.[[:space:]]*(.*)\[vol:[^]]+\]/\1|\2/' |
        xargs -n1 echo
}

# Get current default sink ID
get_current_sink_id() {
    wpctl status | awk '/Default Sink:/ {print $3}'
}

# Get current sink's name
get_current_sink_name() {
    id=$(get_current_sink_id)
    get_sinks | grep "^$id|" | cut -d'|' -f2
}

# MAIN
action="$1"

sinks=($(get_sinks))
sink_ids=()
sink_names=()

for entry in "${sinks[@]}"; do
    id="${entry%%|*}"
    name="${entry#*|}"
    sink_ids+=("$id")
    sink_names+=("$name")
done

current_id=$(get_current_sink_id)
current_index=-1

# Find current index
for i in "${!sink_ids[@]}"; do
    if [[ "${sink_ids[$i]}" == "$current_id" ]]; then
        current_index=$i
        break
    fi
done

# Only show icon if no argument
if [[ -z "$action" ]]; then
    current_name=$(get_current_sink_name)
    get_icon "$current_name"
    exit 0
fi

# Compute new index
if [[ "$action" == "next" ]]; then
    next_index=$(( (current_index + 1) % ${#sink_ids[@]} ))
elif [[ "$action" == "prev" ]]; then
    next_index=$(( (current_index - 1 + ${#sink_ids[@]}) % ${#sink_ids[@]} ))
else
    echo "Unknown action: $action"
    exit 1
fi

# Switch sink
new_id="${sink_ids[$next_index]}"
new_name="${sink_names[$next_index]}"
wpctl set-default "$new_id"
echo "Switched to: $new_name"
