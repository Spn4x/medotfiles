#!/bin/bash

# CLion launcher path from JetBrains Toolbox install
CLION_LAUNCHER="$HOME/.local/share/JetBrains/Toolbox/apps/clion/bin/clion.sh"

# Lock CLion and its spawn to a tight 1.5GB sandbox
systemd-run --user --scope \
  -p MemoryMax=1500M \
  -p MemorySwapMax=512M \
  "$CLION_LAUNCHER"
