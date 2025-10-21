#!/bin/sh

# This is where our script will write its log file.
LOGFILE="/tmp/aurora-launcher-wl-copy.log"

# Log a timestamp and a divider to keep runs separate.
echo "--- $(date) ---" >> "$LOGFILE"

# Log the critical environment variables that Wayland clients need.
echo "WAYLAND_DISPLAY=$WAYLAND_DISPLAY" >> "$LOGFILE"
echo "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR" >> "$LOGFILE"
echo "PATH=$PATH" >> "$LOGFILE"
echo "" >> "$LOGFILE"
echo "--- Data received from stdin: ---" >> "$LOGFILE"

# This is the clever part:
# `tee -a "$LOGFILE"` reads from stdin, passes it through to its own stdout, AND appends a copy to our log file.
# We then pipe that stdout to the REAL wl-copy.
# `2>> "$LOGFILE"` redirects any error messages from wl-copy into our log file.
tee -a "$LOGFILE" | /usr/bin/wl-copy 2>> "$LOGFILE"

echo "--- End of wl-copy execution ---" >> "$LOGFILE"
echo "" >> "$LOGFILE"