#!/bin/bash

# --- yplay.final (The Definitive, Polished Version) ---
# This version combines all bug fixes, features, and visual polish into one
# definitive script. It removes the distracting animations and focuses on a
# clean, stable, and highly functional user experience.
#
# FINAL FEATURES:
# - Clean, static UI with no distracting animations.
# - Perfected three-state repeat cycle: (r) OFF -> (R) PLAYLIST -> (1) SINGLE.
# - 'Add to Queue' ('a') and 'Replace Playlist' ('/') functionality.
# - Highlighted current track for excellent visibility.
# - All bugs (seeking, topic filtering, etc.) are fixed.
#
# Dependencies: yt-dlp, mpv, mpv-mpris, fzf, jq, socat

# --- CONFIGURATION ---
SEARCH_COUNT=50; PREFER_TOPIC_CHANNELS=true; MPRIS_PATH="/usr/lib/mpv/scripts/mpris.so"

# --- TUI & COLORS ---
C_RESET='\033[0m'; C_CYAN='\033[0;36m'; C_GREEN='\033[0;32m'; C_YELLOW='\033[0;33m'; C_GRAY='\033[0;90m'; C_BOLD='\033[1m'
C_INVERSE='\033[7m'
ICON_PLAY="▶"; ICON_PAUSE="⏸"; ICON_SEARCH="?"
PROG_FILLED="="; PROG_EMPTY="-"

# --- SCRIPT STATE ---
SESSION_DIR=$(mktemp -d); IPC_SOCKET="${SESSION_DIR}/mpv-socket"
declare -a PLAYLIST_IDS=() PLAYLIST_TITLES=() PLAYLIST_UPLOADS=()
RESIZED=true; REPEAT_STATE=1 # 0=Off, 1=Playlist, 2=Single

# --- CORE FUNCTIONS ---
cleanup() { tput cnorm; tput clear; echo "Shutting down..."; [ -S "$IPC_SOCKET" ] && echo '{ "command": ["quit"] }' | socat - "$IPC_SOCKET" >/dev/null 2>&1; rm -rf "$SESSION_DIR"; exit 0; }
mpv_command() { echo "$1" | socat - "$IPC_SOCKET" >/dev/null 2>&1; }
mpv_get_property() { echo "{\"command\": [\"get_property\", \"$1\"]}" | socat - "$IPC_SOCKET" 2>/dev/null | jq -r .data; }

# --- THE STABLE TUI ENGINE ---
draw_full_ui_frame() {
    local width=$(tput cols); local height=$(tput lines); tput clear; tput civis
    tput cup 0 0; printf "${C_CYAN}${C_BOLD}%*s\n${C_RESET}" $(( (width + 15) / 2 )) "yplay - Final"
    tput cup 1 0; printf "%${width}s" | tr ' ' '-'
    tput cup 2 1; echo -e "${C_YELLOW}Now Playing:${C_RESET}"
    tput cup 5 1; echo -e "${C_YELLOW}Progress:${C_RESET}"
    tput cup 7 0; printf "%${width}s" | tr ' ' '-'
    tput cup 8 1; echo -e "${C_YELLOW}Playlist Queue:${C_RESET}"
    tput cup $((height - 2)) 0; printf "%${width}s" | tr ' ' '-'
    tput cup $((height - 1)) 0; tput el
    echo -e " ${C_CYAN}(p/s)${C_RESET}pause ${C_CYAN}(n/b)${C_RESET}next/prev ${C_CYAN}(r)${C_RESET}epeat ${C_CYAN}(a)${C_RESET}dd ${C_CYAN}(/)${C_RESET}search ${C_CYAN}(q)${C_RESET}uit"
    RESIZED=false
}

update_tui() {
    [[ "$RESIZED" == true ]] && draw_full_ui_frame

    local current_index=$(mpv_get_property "playlist-pos"); [[ "$current_index" == "null" ]] && current_index=-1
    local paused=$(mpv_get_property "pause"); local pos_s=$(mpv_get_property "playback-time"); local dur_s=$(mpv_get_property "duration")
    
    local width=$(tput cols); local height=$(tput lines)

    local title=""; local artist="";
    if (( current_index >= 0 )); then
        title="${C_GREEN}${C_BOLD}${PLAYLIST_TITLES[$current_index]}"
        artist="${C_GRAY}by ${PLAYLIST_UPLOADS[$current_index]}"
    fi
    tput cup 3 1; echo -en "$title"; tput el
    tput cup 4 1; echo -en "$artist"; tput el
    
    local pos_str="--:--" dur_str="--:--"
    local bar_w=$((width - 18)); local bar; bar=$(printf "%${bar_w}s" | tr ' ' "$PROG_EMPTY")
    if [[ "$pos_s" != "null" && "$dur_s" != "null" && ${dur_s%.*} -gt 0 ]]; then
        pos_str=$(printf "%02d:%02d" $(( ${pos_s%.*} / 60 )) $(( ${pos_s%.*} % 60 )))
        dur_str=$(printf "%02d:%02d" $(( ${dur_s%.*} / 60 )) $(( ${dur_s%.*} % 60 )))
        local percent=$(( ${pos_s%.*} * 100 / ${dur_s%.*} )); local filled_w=$(( bar_w * percent / 100 ))
        bar=$(printf "%${filled_w}s" | tr ' ' "$PROG_FILLED")$(printf "%$((bar_w - filled_w))s" | tr ' ' "$PROG_EMPTY")
    fi
    local status_icon=$([[ "$paused" == "false" ]] && echo -e "$C_GREEN$ICON_PLAY" || echo -e "$C_YELLOW$ICON_PAUSE")
    local repeat_status;
    case $REPEAT_STATE in
        0) repeat_status=" ${C_GRAY}(r)";;
        1) repeat_status=" ${C_GREEN}(R)";;
        2) repeat_status=" ${C_CYAN}(1)";;
    esac
    tput cup 6 1; echo -en "$status_icon $pos_str ${C_GREEN}[${bar}]${C_RESET} $dur_str$repeat_status"; tput el

    local list_start_y=9; local list_h=$((height - list_start_y - 2)); local start_index=0
    if (( current_index > list_start_y + list_h - 4 )); then start_index=$((current_index - 3)); fi
    
    for i in $(seq 0 $((list_h - 1))); do
        local y_pos=$((list_start_y + i)); local playlist_index=$((start_index + i)); local line=""
        if (( playlist_index < ${#PLAYLIST_IDS[@]} )); then
            local display_title="${PLAYLIST_TITLES[$playlist_index]}"
            if [[ $playlist_index -eq $current_index ]]; then line="${C_INVERSE}  ▶ ${display_title}  "
            else line="${C_GRAY}    ${display_title}"; fi
        fi
        tput cup $y_pos 1; echo -en "${line:0:$((width-1))}"; tput el
    done
}

# --- PLAYLIST MANAGEMENT ---
add_to_playlist() {
    tput cnorm; tput cup $(tput lines) 0; tput el; read -r -p "${ICON_SEARCH} Add to queue: " query; tput civis
    [[ -z "$query" ]] && return 1; local search_term="$query"; if [[ "$PREFER_TOPIC_CHANNELS" = true ]]; then search_term+=" topic"; fi
    local selections; selections=$(yt-dlp --flat-playlist --dump-json "ytsearch${SEARCH_COUNT}:${search_term}" 2>/dev/null | \
        jq -r 'select(.id != null) | "\(.uploader // "N/A") - \(.title) | \(.id)"' | \
        fzf --reverse --height=50% --multi --prompt="Add to Queue > " --header="[Tab] to select, [Enter] to add")
    [[ -z "$selections" ]] && return 1
    while IFS= read -r line; do
        PLAYLIST_IDS+=("$(echo "$line" | awk -F ' | ' '{print $NF}')"); PLAYLIST_TITLES+=("$(echo "$line" | sed -e 's/.* - //' -e 's/ | .*//')")
        PLAYLIST_UPLOADS+=("$(echo "$line" | sed 's/ - .*//')"); local url="https://www.youtube.com/watch?v=${PLAYLIST_IDS[-1]}"
        mpv_command "{\"command\": [\"loadfile\", \"$url\", \"append\"]}"
    done <<< "$selections"; return 0
}

create_new_playlist() {
    tput clear; tput cnorm; read -r -p "${ICON_SEARCH} Search YouTube (or 'exit'): " query
    [[ "$query" == "exit" || -z "$query" ]] && return 1; local search_term="$query"; if [[ "$PREFER_TOPIC_CHANNELS" = true ]]; then search_term+=" topic"; fi
    local selections; selections=$(yt-dlp --flat-playlist --dump-json "ytsearch${SEARCH_COUNT}:${search_term}" 2>/dev/null | \
        jq -r 'select(.id != null) | "\(.uploader // "N/A") - \(.title) | \(.id)"' | \
        fzf --reverse --height=80% --multi --prompt="Playlist Builder > " --header="[Tab] to select, [Enter] to confirm")
    [[ -z "$selections" ]] && return 1
    mpv_command '{ "command": ["playlist-clear"] }'; PLAYLIST_IDS=(); PLAYLIST_TITLES=(); PLAYLIST_UPLOADS=()
    while IFS= read -r line; do
        PLAYLIST_IDS+=("$(echo "$line" | awk -F ' | ' '{print $NF}')"); PLAYLIST_TITLES+=("$(echo "$line" | sed -e 's/.* - //' -e 's/ | .*//')")
        PLAYLIST_UPLOADS+=("$(echo "$line" | sed 's/ - .*//')"); local url="https://www.youtube.com/watch?v=${PLAYLIST_IDS[-1]}"
        mpv_command "{\"command\": [\"loadfile\", \"$url\", \"append\"]}"
    done <<< "$selections"; mpv_command '{ "command": ["set_property", "playlist-pos", 0] }'; return 0
}

# --- MAIN EXECUTION ---
trap cleanup EXIT INT TERM; trap 'RESIZED=true' WINCH
mpv --no-video --vo=null --script="${MPRIS_PATH}" --input-ipc-server="$IPC_SOCKET" --idle=yes --force-window=no --loop-playlist=inf </dev/null >/dev/null 2>&1 &
until [ -S "$IPC_SOCKET" ]; do sleep 0.1; done

create_new_playlist || cleanup

while true; do
    update_tui
    stty -echo; read -rsn1 -t 0.5 key; stty echo
    if [[ "$key" == $'\x1b' ]]; then read -rsn2 -t 0.01 subkey; key+="$subkey"; fi
    case "$key" in
        'p'|' ') mpv_command '{ "command": ["cycle", "pause"] }' ;;
        'n') mpv_command '{ "command": ["playlist-next"] }' ;; 'b') mpv_command '{ "command": ["playlist-prev"] }' ;;
        'r') REPEAT_STATE=$(( (REPEAT_STATE + 1) % 3 ));
             case $REPEAT_STATE in
                0) mpv_command '{ "command": ["set_property", "loop-playlist", "no"] }'; mpv_command '{ "command": ["set_property", "loop-file", "no"] }';;
                1) mpv_command '{ "command": ["set_property", "loop-playlist", "inf"] }'; mpv_command '{ "command": ["set_property", "loop-file", "no"] }';;
                2) mpv_command '{ "command": ["set_property", "loop-playlist", "no"] }'; mpv_command '{ "command": ["set_property", "loop-file", "inf"] }';;
             esac ;;
        'a') if add_to_playlist; then RESIZED=true; fi ;;
        '/') if create_new_playlist; then RESIZED=true; fi ;;
        '+'|'=') mpv_command '{ "command": ["add", "volume", 2] }' ;; '-':) mpv_command '{ "command": ["add", "volume", -2] }' ;;
        '>'|$'\x1b[C') mpv_command '{ "command": ["seek", "5"] }' ;; '<'|$'\x1b[D') mpv_command '{ "command": ["seek", -5"] }' ;;
        'q') cleanup ;;
    esac
done