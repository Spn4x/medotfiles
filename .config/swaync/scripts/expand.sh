#!/bin/bash

# This script writes the EXPANDED config to config.json and reloads swaync.

# Define the config file path
CONFIG_FILE="$HOME/.config/swaync/config.json"

# Write the expanded config using a heredoc
cat > "$CONFIG_FILE" << EOF
{
"$schema": "/etc/xdg/swaync/configSchema.json",
"positionX": "center",
"positionY": "top",
"layer": "overlay",
"control-center-layer": "top",
"layer-shell": true,
"cssPriority": "application",

    "control-center-width": 695,
    "control-center-height": 675, 
    "control-center-margin-top": 0,
    "control-center-margin-bottom": 40,
    "control-center-margin-right": 1,
    "control-center-margin-left": 0,
  	
    "notification-2fa-action": true,
  	"notification-inline-replies": true,
    "notification-window-width": 300,
    "notification-icon-size": 50,
    "notification-body-image-height":  140,
    "notification-body-image-width": 180,
    
    "timeout": 6,
    "timeout-low": 3,
    "timeout-critical": 0,
    "fit-to-screen": false,
    "keyboard-shortcuts": true,
    "image-visibility": "when available",
    "transition-time": 200,
    "hide-on-clear": true,
    "hide-on-action": true,
    "script-fail-notify": true,
    "widgets": [
         "mpris",
         "buttons-grid",
         "volume", 
         "backlight",
         "title",
         "notifications"
    ],
    "widget-config": {
        "title": {
            "text": "Notifications",
            "clear-all-button": true,
            "button-text": "Clear"
        },
        "dnd": {
            "text": " "
        },
        "label": {
            "max-lines":2,
            "text": "Notification"
        },
        "mpris": {
            "image-size": 60,
            "image-radius": 10
        },
        "volume": {
            "label": "󰕾"
        },
        "backlight": {
            "label": "󰃟"
        },
        "buttons-grid": {
            "actions": [
              {
                    "label": " ",
                    "command": "swaync-client -t && env GTK_THEME=Adwaita:dark WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS nm-connection-editor"
                },
                {
                    "label": "󰂯",
                    "command": "swaync-client -t && env GTK_THEME=Adwaita:dark WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS blueman-manager"
                },
                                {
                    "label": " ",
                    "command": "swaync-client -t && hyprlock"
                },
                {
                    "label": "󰤄",
                    "command": "swaync-client -t && systemctl suspend"
                },
                {
                    "label": " ",
                    "command": "swaync-client -t && hyprctl dispatch exit"
                },
                {
                    "label": "",
                    "command": "swaync-client -t && systemctl reboot"
                },
                {
                    "label": "",
                    "command": "swaync-client -t && systemctl poweroff"
                },
                   {
                    "label": "󰅂",
                    "command": "bash ~/.config/swaync/scripts/collapse.sh"
                }
            ]
        }
    }
}
EOF

swaync-client -R && swaync-client -t && swaync-client -t
