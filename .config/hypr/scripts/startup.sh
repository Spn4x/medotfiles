#!/usr/bin/env bash
# ======================================================================================
#            ROBUST HYPRLAND STARTUP SCRIPT
#
#    This script launches all necessary services and applications for the
#    Hyprland session. It ensures that processes are properly daemonized
#    to prevent them from being killed when the script exits.
#
#    Usage:
#    1. Save this file as ~/.config/hypr/scripts/startup.sh
#    2. Make it executable: chmod +x ~/.config/hypr/scripts/startup.sh
#    3. Add `exec-once = ~/.config/hypr/scripts/startup.sh` to your hyprland.conf
#
# ======================================================================================


# -----------------------------------------------------
# Kill existing processes before starting new ones
# (Prevents duplicates on Hyprland reload)
# -----------------------------------------------------
echo "--- Terminating existing user processes ---"
killall -q ironbar
killall -q swww-daemon
killall -q nm-applet
killall -q blueman-applet
killall -q hypr-wellbeing
killall -q polkit-gnome-authentication-agent-1
pkill -f 'wl-paste --watch cliphist store'
pkill -f 'archbadge.py'



# --------------------------------------------------------------------------------------
# A. UI ELEMENTS & USER APPLICATIONS (LAUNCH FIRST FOR IMMEDIATE VISUALS)
# --------------------------------------------------------------------------------------
echo "--- Launching UI elements and user applications ---"
ironbar &
nm-applet --indicator &
blueman-applet &
python ~/.config/hypr/python/fetchapp/archbadge.py &
systemctl --user restart mpris-watcher.service &
vicinae server & 

# --------------------------------------------------------------------------------------
# B. ENVIRONMENT & SYSTEM SERVICES
# --------------------------------------------------------------------------------------
echo "--- Initializing environment and system services ---"
# Set up D-Bus and systemd environments
systemctl --user import-environment DISPLAY WAYLAND_DISPLAY SWAYSOCK XDG_CURRENT_DESKTOP
hash dbus-update-activation-environment 2>/dev/null && \
  dbus-update-activation-environment --systemd --all

# Polkit (Authentication Agent)
( /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 & )

# Keyring
( gnome-keyring-daemon --start --components=pkcs11,secrets,ssh,gpg & )

# --------------------------------------------------------------------------------------
# C. CORE DAEMONS (BACKGROUND SERVICES)
# --------------------------------------------------------------------------------------
echo "--- Starting core daemons ---"
hypridle &
swww-daemon &
wl-paste --watch cliphist store &
# emacs --daemon & back to vs code normie sorry
(
  export QT_QPA_PLATFORMTHEME=qt6ct
  kdeconnectd &
)

# --------------------------------------------------------------------------------------
# D. FINAL THEMING & WALLPAPER SETUP
# --------------------------------------------------------------------------------------
echo "--- Pausing before applying theme ---"
sleep 3
echo "--- Applying theme and setting wallpaper ---"
~/.config/hypr/scripts/wallpaper.sh --startup

echo "--- Startup script finished successfully ---"

# -----------------------------------------------------
# 1. Environment and System Services
# -----------------------------------------------------
#echo "--- Initializing environment and system services ---"
# Set up D-Bus and systemd environments
#systemctl --user import-environment DISPLAY WAYLAND_DISPLAY SWAYSOCK XDG_CURRENT_DESKTOP
#hash dbus-update-activation-environment 2>/dev/null && dbus-update-activation-environment --systemd --all

# Polkit (Authentication Agent) - MUST BE STARTED FIRST
#( /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 & )

# Keyring
#( gnome-keyring-daemon --start --components=pkcs11,secrets,ssh,gpg & )

# -----------------------------------------------------
# 2. Core Daemons (Background Services)
# -----------------------------------------------------
#echo "--- Starting core daemons ---"
# Idle manager
#( hypridle & )

# Wallpaper daemon
#( swww-daemon & )

# Clipboard manager
#( wl-paste --watch cliphist store & )

# Emacs daemon
#( emacs --daemon & )

# KDE Connect daemon (for clipboard sync, file transfer, etc.)
#(
#  export QT_QPA_PLATFORMTHEME=qt6ct
 # kdeconnectd &
#)


# -----------------------------------------------------
# 3. UI Elements & User Apps
# -----------------------------------------------------
#echo "--- Launching UI elements and user applications ---"

# Sidebar (via toggle script to ensure correct behavior)
#( ~/scripts/toggle-sidebar.sh & )

# Status bar
#( ironbar & )

# Network and Bluetooth tray applets
#( nm-applet --indicator & )
#( blueman-applet & )

# Custom scripts
#( hypr-wellbeing -d &> /dev/null & )
#( python ~/.config/hypr/python/fetchapp/archbadge.py & )


# -----------------------------------------------------
# 4. Final Theming and Wallpaper Setup
# -----------------------------------------------------
#echo "--- Pausing for 3 seconds before applying theme ---"
#sleep 3

#echo "--- Applying theme and setting wallpaper ---"
# This script does not need to be daemonized, as it runs and exits.
#~/.config/hypr/scripts/wallpaper.sh --startup

#echo "--- Startup script finished successfully ---"