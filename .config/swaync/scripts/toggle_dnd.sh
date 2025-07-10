#!/bin/bash

# Toggle DND state
swaync-client -d -t

# Check the new DND status
DND_STATUS=$(swaync-client -d -gs)

# Print to stdout so swaync can update the button state
if [ "$DND_STATUS" = "true" ]; then
    echo '{"text": " DND", "alt": "DND-on", "class": "active"}'
else
    echo '{"text": " DND", "alt": "DND-off", "class": "inactive"}'
fi
