
#!/bin/bash

PIDFILE="/tmp/autosprint.pid"

if [ -f "$PIDFILE" ]; then
  echo "Stopping auto sprint..."
  kill "$(cat $PIDFILE)"
  rm $PIDFILE
else
  echo "Starting auto sprint..."
  while true; do
    wtype -d 10 w
    sleep 0.05
  done &
  echo $! > "$PIDFILE"
fi
