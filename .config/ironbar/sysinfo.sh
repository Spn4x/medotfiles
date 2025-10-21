#!/usr/bin/env bash

# ─── CPU USAGE (delta from last run) ─────────────────────────────────────────────
cpu_state_file="/tmp/.cpu_stat_prev"

read _ u n s i io ir so st _ < /proc/stat
total=$((u + n + s + i + io + ir + so + st))
used=$((total - i))

if [[ -f $cpu_state_file ]]; then
  read prev_total prev_used < "$cpu_state_file"
  dt=$((total - prev_total))
  du=$((used - prev_used))
  cpu_pct=$(( 100 * du / dt ))
else
  cpu_pct=0  # First run
fi
echo "$total $used" > "$cpu_state_file"

# ─── MEMORY USAGE ────────────────────────────────────────────────────────────────
read _ mem_total _ < <(grep MemTotal /proc/meminfo)
read _ mem_avail _ < <(grep MemAvailable /proc/meminfo)
mem_used=$((mem_total - mem_avail))
mem_pct=$(( mem_used * 100 / mem_total ))

# ─── TEMPERATURE ─────────────────────────────────────────────────────────────────
if [[ -f /sys/class/thermal/thermal_zone0/temp ]]; then
  temp_c=$(( $(< /sys/class/thermal/thermal_zone0/temp) / 1000 ))
else
  temp_c="N/A"
fi

# ─── BATTERY STATUS ──────────────────────────────────────────────────────────────
upower_dev=$(upower -e | grep -E 'BAT|battery' | head -n1)
if [[ -n $upower_dev ]]; then
  read -r state pct < <(upower -i "$upower_dev" | awk '
    /state:/      { st = $2 }
    /percentage:/ { gsub("%", "", $2); pc = int($2) }
    END           { print st, pc }
  ')
else
  state="N/A"
  pct=0
fi

case "$state" in
  charging)     bat_icon="󰂄" ;;
  discharging)
    if   (( pct >= 90 )); then bat_icon="󰁹"
    elif (( pct >= 60 )); then bat_icon="󰁼"
    elif (( pct >= 30 )); then bat_icon="󰁻"
    else                       bat_icon="󰁿"
    fi ;;
  fully-charged) bat_icon="󰁹" ;;
  *)             bat_icon="󰁿" ;;
esac

# ─── OUTPUT ──────────────────────────────────────────────────────────────────────
printf "󰍛 %d%% 󰾆 %d%% 󰔏 %s°C %s %d%%\n" "$cpu_pct" "$mem_pct" "$temp_c" "$bat_icon" "$pct"
