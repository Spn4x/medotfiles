#!/bin/bash

# Script to:
# 1. On --startup: Only ensure swww-daemon is running.
# 2. Interactively (no args): Select and set new wallpaper via wofi, then apply themes.
# 3. Cyclically (--next/--prev): Set the next/previous wallpaper, then apply themes.
# 4. With --mute: Run silently without sending notifications.

# --- Configuration ---
WALLPAPER_DIR="$HOME/Pictures/Wallpapers"
WOFI_PROMPT="Select Wallpaper"

# --- swww Transition Options (for interactive mode) ---
TRANSITION_TYPE="grow"
TRANSITION_STEP=10
TRANSITION_FPS=60
TRANSITION_BEZIER="0.7,0.8,2,3"

# --- File Paths ---
SCRIPT_COLORS_RAW="$HOME/.cache/wallust/scriptable_colors.txt"
HYPR_COLORS_OUTPUT="$HOME/.config/hypr/colors-hyprland-generated.conf"
WOFI_STYLE_BASE="$HOME/.config/wofi/style-base.css"
WOFI_STYLE_OUTPUT="$HOME/.config/wofi/style-wallust-generated.css"
IRONBAR_STYLE_TEMPLATE="$HOME/.config/ironbar/style-wallust-generated.css" # Assuming this is the template
IRONBAR_STYLE_OUTPUT="$HOME/.config/ironbar/style.css"       # And this is the final output
NVIM_THEME_OUTPUT="$HOME/.config/nvim/lua/wallust.lua"
# --- GLAVA (Hidden) ---
# GLAVA_BARS_SHADER_FILE="$HOME/.config/glava/bars.glsl"
# GLAVA_RADIAL_SHADER_FILE="$HOME/.config/glava/radial.glsl" # <- Added the missing quote here
# --- SWAYNC START ---
SWAYNC_STYLE_BASE="$HOME/.config/swaync/style-base.css"
SWAYNC_STYLE_OUTPUT="$HOME/.config/swaync/style.css"
# --- SWAYNC END ---
# --- CHEATSHEET START ---
CHEATSHEET_STYLE_TEMPLATE="$HOME/.config/hypr/python/cheatsheet-template.css"
CHEATSHEET_STYLE_OUTPUT="$HOME/.config/hypr/python/cheatsheet.css"
# --- CHEATSHEET END ---
ARCHBADGE_STYLE_TEMPLATE="$HOME/.config/hypr/python/fetchapp/archbadge-template.css"
ARCHBADGE_STYLE_OUTPUT="$HOME/.config/hypr/python/fetchapp/archbadge.css"
# --- C-WIDGETS SIDEBAR START ---
# SIDEBAR_STYLE_TEMPLATE="$HOME/.config/hypr/C-widgets/sidebar/src/style-template.css"
# SIDEBAR_STYLE_OUTPUT="$HOME/.config/hypr/C-widgets/sidebar/src/style.css"
# --- C-WIDGETS SIDEBAR END ---

# --- C-WIDGETS SIDEBAR END ---
# --- C-WIDGETS LAUNCHER START ---
CACHY_STYLE_TEMPLATE="$HOME/.config/hypr/C-widgets/archlauncher-c/cachy-template.css"
CACHY_STYLE_OUTPUT="$HOME/.config/hypr/C-widgets/archlauncher-c/cachy.css"
LAUNCHER_STYLE_TEMPLATE="$HOME/.config/hypr/C-widgets/archlauncher-c/launcher-template.css"
LAUNCHER_STYLE_OUTPUT="$HOME/.config/hypr/C-widgets/archlauncher-c/launcher.css"
# --- C-WIDGETS LAUNCHER END ---

# --- C-WIDGETS CALENDAR START ---
HYPER_CALENDAR_STYLE_TEMPLATE="$HOME/.config/hypr/C-widgets/hyper-calendar/src/style-template.css"
HYPER_CALENDAR_STYLE_OUTPUT="$HOME/.config/hypr/C-widgets/hyper-calendar/src/style.css"
# --- C-WIDGETS CALENDAR END ---

# --- C-WIDGETS CONTROL-CENTER START ---
CC_STYLE_TEMPLATE="$HOME/.config/hypr/C-widgets/controlcenter-remake/src/style-template.css"
CC_STYLE_OUTPUT="$HOME/.config/hypr/C-widgets/controlcenter-remake/src/style.css"
# --- C-WIDGETS CONTROL-CENTER END ---

# --- C-WIDGETS SCHEDULE-WIDGET START ---
SCHEDULE_WIDGET_STYLE_TEMPLATE="$HOME/.config/hypr/C-widgets/schedule-widget/data/style-template.css"
SCHEDULE_WIDGET_STYLE_OUTPUT="$HOME/.config/hypr/C-widgets/schedule-widget/data/style.css"
# --- C-WIDGETS SCHEDULE-WIDGET END ---

# --- C-WIDGETS SIDE-MPRIS-PLAYER START ---
MPRIS_PLAYER_STYLE_TEMPLATE="$HOME/.config/hypr/C-widgets/side-mpris-player/src/style-template.css"
MPRIS_PLAYER_STYLE_OUTPUT="$HOME/.config/hypr/C-widgets/side-mpris-player/src/style.css"
# --- C-WIDGETS SIDE-MPRIS-PLAYER END ---

# --- NEW: Lock File and Mute Flag ---
LOCK_FILE="/tmp/wallpaper.lock"
NOTIFICATIONS_MUTED=false
# ---

# --- NEW: Function to handle notifications ---
# This wrapper function checks the mute flag before sending a notification.
send_notification() {
    if [ "$NOTIFICATIONS_MUTED" = "false" ]; then
        notify-send "$@"
    fi
}
# ---

# --- Theme Application Function ---
apply_theme_and_reload() {
    local SELECTED_NEW_WALLPAPER_PATH="$1"

    if [ -z "$SELECTED_NEW_WALLPAPER_PATH" ] || [ ! -f "$SELECTED_NEW_WALLPAPER_PATH" ]; then
        send_notification -u critical "Wallpaper Script Error" "Invalid wallpaper path provided."
        return 1
    fi

    random_x=$(awk -v seed=$RANDOM 'BEGIN { srand(seed); printf "%.2f\n", rand() }')
    random_y=$(awk -v seed=$RANDOM 'BEGIN { srand(seed); printf "%.2f\n", rand() }')
    random_pos="${random_x},${random_y}"

    echo "Setting new wallpaper: $SELECTED_NEW_WALLPAPER_PATH (grow from $random_pos)"
    swww img "$SELECTED_NEW_WALLPAPER_PATH" \
        --transition-type "$TRANSITION_TYPE" \
        --transition-fps "$TRANSITION_FPS" \
        --transition-step "$TRANSITION_STEP" \
        --transition-pos "$random_pos" \
        ${TRANSITION_BEZIER:+--transition-bezier "$TRANSITION_BEZIER"}

    if [ $? -ne 0 ]; then send_notification -u critical "SWWW Error setting new wallpaper"; return 1; fi
    echo "New wallpaper set via swww."

    send_notification -u low "🎨 Generating Palette" "Extracting colors using Wallust..."

    echo "Running wallust for theming: $SELECTED_NEW_WALLPAPER_PATH"
    wallust run  --backend wal "$SELECTED_NEW_WALLPAPER_PATH"
    if [ $? -ne 0 ]; then send_notification -u critical "Wallust Error during theming"; return 1; fi
    echo "Wallust schemes generated based on $SELECTED_NEW_WALLPAPER_PATH."

    if [ -f "$SCRIPT_COLORS_RAW" ]; then
        set +u
        source "$SCRIPT_COLORS_RAW"
        set -u
        echo "Sourced colors from $SCRIPT_COLORS_RAW"

        send_notification -u low "🖌️ Applying Theme" "Generating new configuration files..."

        # --- Hyprland Colors ---
        if [ -n "$color4" ] && [ -n "$color6" ] && [ -n "$color0" ] && [ -n "$background" ] && [ -n "$foreground" ] && [ -n "$cursor" ]; then
            echo "Generating Hyprland colors..."
            active_border_col1_hex=${color4#\#}
            active_border_col2_hex=${color6#\#}
            inactive_border_col_hex=${color0#\#}
            cat > "$HYPR_COLORS_OUTPUT" << EOF
# Hyprland colors generated by wallpaper.sh
\$wallust_background = $background
\$wallust_foreground = $foreground
\$wallust_cursor = $cursor
\$wallust_color0 = $color0
\$wallust_color1 = $color1
\$wallust_color2 = $color2
\$wallust_color3 = $color3
\$wallust_color4 = $color4
\$wallust_color5 = $color5
\$wallust_color6 = $color6
\$wallust_color7 = $color7
\$wallust_color8 = $color8
\$wallust_color9 = $color9
\$wallust_color10 = $color10
\$wallust_color11 = $color11
\$wallust_color12 = $color12
\$wallust_color13 = $color13
\$wallust_color14 = $color14
\$wallust_color15 = $color15
general {
    col.active_border = rgba(${active_border_col1_hex}ff) rgba(${active_border_col2_hex}ff) 45deg
    col.inactive_border = rgba(${inactive_border_col_hex}aa)
}
EOF
            echo "Hyprland colors written to $HYPR_COLORS_OUTPUT"
        else
            echo "Warning: Hyprland color vars not fully loaded from SCRIPT_COLORS_RAW. Skipping Hyprland."
            echo "Missing vars for Hyprland: color4='$color4', color6='$color6', color0='$color0', background='$background', foreground='$foreground', cursor='$cursor'"
        fi

               # --- Wofi Colors ---
        # if [ -f "$WOFI_STYLE_BASE" ]; then
        #     # Check for required wallust colors
        #     if [ -n "$background" ] && [ -n "$foreground" ] && [ -n "$color0" ] && [ -n "$color4" ] && [ -n "$color8" ]; then
        #         echo "Generating Wofi CSS with RGBA palette..."
        #
        #         # Helper function to convert #RRGGBB to rgba(r,g,b,alpha)
        #         hex_to_rgba() {
        #             local hex=${1#\#}
        #             local alpha=$2
        #             local r=$((16#${hex:0:2}))
        #             local g=$((16#${hex:2:2}))
        #             local b=$((16#${hex:4:2}))
        #             echo "rgba($r, $g, $b, $alpha)"
        #         }
        #
        #         # Generate all the required RGBA colors from hex codes
        #         background_rgba_30=$(hex_to_rgba "$background" "0.3") # 30% Opacity Background
        #         accent_rgba_80=$(hex_to_rgba "$color4" "0.8")         # 80% Opacity Accent
        #
        #         tmp_wofi_css=$(mktemp)
        #         cp "$WOFI_STYLE_BASE" "$tmp_wofi_css"
        #
        #         # --- Replace all placeholders ---
        #         # Solid colors (for flexibility, in case you still use them in the CSS)
        #         sed -i "s|%%BACKGROUND%%|$background|g" "$tmp_wofi_css"
        #         sed -i "s|%%FOREGROUND%%|$foreground|g" "$tmp_wofi_css"
        #         sed -i "s|%%ACCENT_COLOR%%|$color4|g" "$tmp_wofi_css"
        #         sed -i "s|%%INPUT_BG%%|$color0|g" "$tmp_wofi_css"
        #         sed -i "s|%%BORDER_SUBTLE%%|$color8|g" "$tmp_wofi_css"
        #         
        #         # New RGBA colors
        #         sed -i "s|%%BACKGROUND_RGBA_30%%|$background_rgba_30|g" "$tmp_wofi_css"
        #         sed -i "s|%%ACCENT_RGBA_80%%|$accent_rgba_80|g" "$tmp_wofi_css"
        #
        #         mv "$tmp_wofi_css" "$WOFI_STYLE_OUTPUT"
        #         echo "Wofi CSS written to $WOFI_STYLE_OUTPUT"
        #     else
        #         echo "Warning: Wofi color vars not fully loaded. Skipping Wofi."
        #     fi
        # else
        #     echo "Error: Wofi base style $WOFI_STYLE_BASE not found."
        # fi

        # --- Ironbar Colors ---
        if [ -n "$background" ] && [ -n "$foreground" ] && [ -n "$cursor" ] && \
           [ -n "$color0" ] && [ -n "$color1" ] && [ -n "$color2" ] && [ -n "$color3" ] && \
           [ -n "$color4" ] && [ -n "$color5" ] && [ -n "$color6" ] && [ -n "$color7" ] && \
           [ -n "$color8" ] && [ -n "$color9" ] && [ -n "$color10" ] && [ -n "$color11" ] && \
           [ -n "$color12" ] && [ -n "$color13" ] && [ -n "$color14" ] && [ -n "$color15" ]; then
            echo "Generating Ironbar CSS..."
            if [ -f "$IRONBAR_STYLE_TEMPLATE" ]; then
                tmp_ironbar_css=$(mktemp)
                cp "$IRONBAR_STYLE_TEMPLATE" "$tmp_ironbar_css"
                sed -i "s|{{background}}|$background|g" "$tmp_ironbar_css"
                sed -i "s|{{foreground}}|$foreground|g" "$tmp_ironbar_css"
                sed -i "s|{{cursor}}|$cursor|g" "$tmp_ironbar_css"
                sed -i "s|{{color0}}|$color0|g" "$tmp_ironbar_css"
                sed -i "s|{{color1}}|$color1|g" "$tmp_ironbar_css"
                sed -i "s|{{color2}}|$color2|g" "$tmp_ironbar_css"
                sed -i "s|{{color3}}|$color3|g" "$tmp_ironbar_css"
                sed -i "s|{{color4}}|$color4|g" "$tmp_ironbar_css"
                sed -i "s|{{color5}}|$color5|g" "$tmp_ironbar_css"
                sed -i "s|{{color6}}|$color6|g" "$tmp_ironbar_css"
                sed -i "s|{{color7}}|$color7|g" "$tmp_ironbar_css"
                sed -i "s|{{color8}}|$color8|g" "$tmp_ironbar_css"
                sed -i "s|{{color9}}|$color9|g" "$tmp_ironbar_css"
                sed -i "s|{{color10}}|$color10|g" "$tmp_ironbar_css"
                sed -i "s|{{color11}}|$color11|g" "$tmp_ironbar_css"
                sed -i "s|{{color12}}|$color12|g" "$tmp_ironbar_css"
                sed -i "s|{{color13}}|$color13|g" "$tmp_ironbar_css"
                sed -i "s|{{color14}}|$color14|g" "$tmp_ironbar_css"
                sed -i "s|{{color15}}|$color15|g" "$tmp_ironbar_css"
                mv "$tmp_ironbar_css" "$IRONBAR_STYLE_OUTPUT"
                echo "Ironbar CSS written to $IRONBAR_STYLE_OUTPUT"
            else
                echo "Error: Ironbar template $IRONBAR_STYLE_TEMPLATE not found."
            fi
        else
            echo "Warning: Ironbar color vars not fully loaded from SCRIPT_COLORS_RAW. Skipping Ironbar."
        fi

          # --- CHEATSHEET START ---
    if [ -n "$background" ] && [ -n "$foreground" ] && [ -n "$color4" ] && [ -n "$color0" ]; then
        echo "Generating Cheatsheet CSS..."
        if [ -f "$CHEATSHEET_STYLE_TEMPLATE" ]; then
            tmp_cheatsheet_css=$(mktemp)
            cp "$CHEATSHEET_STYLE_TEMPLATE" "$tmp_cheatsheet_css"
            sed -i "s|{{background}}|$background|g" "$tmp_cheatsheet_css"
            sed -i "s|{{foreground}}|$foreground|g" "$tmp_cheatsheet_css"
            sed -i "s|{{color4}}|$color4|g" "$tmp_cheatsheet_css"
            sed -i "s|{{color0}}|$color0|g" "$tmp_cheatsheet_css"
            mv "$tmp_cheatsheet_css" "$CHEATSHEET_STYLE_OUTPUT"
            echo "Cheatsheet CSS written to $CHEATSHEET_STYLE_OUTPUT"
        else
            echo "Error: Cheatsheet template $CHEATSHEET_STYLE_TEMPLATE not found."
        fi
    else
        echo "Warning: Cheatsheet color vars not fully loaded. Skipping Cheatsheet."
    fi
    # --- CHEATSHEET END ---

        # --- Arch Badge CSS ---
        if [ -f "$ARCHBADGE_STYLE_TEMPLATE" ]; then
            echo "Generating Arch Badge CSS..."
            tmp_badge_css=$(mktemp)
            cp "$ARCHBADGE_STYLE_TEMPLATE" "$tmp_badge_css"
            
            # This comprehensive block replaces ALL possible color placeholders.
            sed -i "s|{{background}}|$background|g" "$tmp_badge_css"
            sed -i "s|{{foreground}}|$foreground|g" "$tmp_badge_css"
            sed -i "s|{{cursor}}|$cursor|g" "$tmp_badge_css"
            sed -i "s|{{color0}}|$color0|g" "$tmp_badge_css"
            sed -i "s|{{color1}}|$color1|g" "$tmp_badge_css"
            sed -i "s|{{color2}}|$color2|g" "$tmp_badge_css"
            sed -i "s|{{color3}}|$color3|g" "$tmp_badge_css"
            sed -i "s|{{color4}}|$color4|g" "$tmp_badge_css"
            sed -i "s|{{color5}}|$color5|g" "$tmp_badge_css"
            sed -i "s|{{color6}}|$color6|g" "$tmp_badge_css"
            sed -i "s|{{color7}}|$color7|g" "$tmp_badge_css"
            sed -i "s|{{color8}}|$color8|g" "$tmp_badge_css"
            sed -i "s|{{color9}}|$color9|g" "$tmp_badge_css"
            sed -i "s|{{color10}}|$color10|g" "$tmp_badge_css"
            sed -i "s|{{color11}}|$color11|g" "$tmp_badge_css"
            sed -i "s|{{color12}}|$color12|g" "$tmp_badge_css"
            sed -i "s|{{color13}}|$color13|g" "$tmp_badge_css"
            sed -i "s|{{color14}}|$color14|g" "$tmp_badge_css"
            sed -i "s|{{color15}}|$color15|g" "$tmp_badge_css"

            mv "$tmp_badge_css" "$ARCHBADGE_STYLE_OUTPUT"
            echo "Arch Badge CSS written to $ARCHBADGE_STYLE_OUTPUT"
        else
            echo "Error: Arch Badge template $ARCHBADGE_STYLE_TEMPLATE not found."
        fi


               # --- C-WIDGETS SIDEBAR START ---
       # if [ -f "$SIDEBAR_STYLE_TEMPLATE" ]; then
        #    echo "Generating C-Widgets Sidebar CSS..."
        #    tmp_sidebar_css=$(mktemp)
        #    cp "$SIDEBAR_STYLE_TEMPLATE" "$tmp_sidebar_css"
            
            # Map wallust colors to the named colors in the CSS template
            # 'accent' is the main theme color, usually color4
            # 'surface' is a secondary background, usually color0
            # 'warning' is for errors, usually red, which is color1
     #       sed -i "s|%%BACKGROUND%%|$background|g" "$tmp_sidebar_css"
    #        sed -i "s|%%FOREGROUND%%|$foreground|g" "$tmp_sidebar_css"
   #         sed -i "s|%%ACCENT%%|$color4|g" "$tmp_sidebar_css"
  #          sed -i "s|%%SURFACE%%|$color0|g" "$tmp_sidebar_css"
 #           sed -i "s|%%WARNING%%|$color1|g" "$tmp_sidebar_css"
#
     #       mv "$tmp_sidebar_css" "$SIDEBAR_STYLE_OUTPUT"
    #        echo "Sidebar CSS written to $SIDEBAR_STYLE_OUTPUT"
   #     else
  #          echo "Error: Sidebar template $SIDEBAR_STYLE_TEMPLATE not found."
  #      fi
        # --- C-WIDGETS SIDEBAR END ---
        
         # --- C-WIDGETS LAUNCHER START ---
        # This block themes the cachy image selector and the app launcher.
        if [ -n "$background" ] && [ -n "$foreground" ] && [ -n "$color4" ] && [ -n "$color0" ]; then
            # --- Cachy Image Selector ---
            if [ -f "$CACHY_STYLE_TEMPLATE" ]; then
                echo "Generating Cachy Selector CSS..."
                tmp_cachy_css=$(mktemp)
                cp "$CACHY_STYLE_TEMPLATE" "$tmp_cachy_css"
                
                sed -i "s|{{background}}|$background|g" "$tmp_cachy_css"
                sed -i "s|{{foreground}}|$foreground|g" "$tmp_cachy_css"
                # In your templates, 'accent' is the main highlight and 'surface' is the secondary background.
                # Mapping them to color4 and color0 is a great choice.
                sed -i "s|{{accent}}|$color4|g" "$tmp_cachy_css"
                sed -i "s|{{surface}}|$color0|g" "$tmp_cachy_css"

                mv "$tmp_cachy_css" "$CACHY_STYLE_OUTPUT"
                echo "Cachy Selector CSS written to $CACHY_STYLE_OUTPUT"
            else
                echo "Error: Cachy template $CACHY_STYLE_TEMPLATE not found."
            fi

            # --- Arch Launcher ---
            if [ -f "$LAUNCHER_STYLE_TEMPLATE" ]; then
                echo "Generating Arch Launcher CSS..."
                tmp_launcher_css=$(mktemp)
                cp "$LAUNCHER_STYLE_TEMPLATE" "$tmp_launcher_css"
                
                sed -i "s|{{background}}|$background|g" "$tmp_launcher_css"
                sed -i "s|{{foreground}}|$foreground|g" "$tmp_launcher_css"
                sed -i "s|{{accent}}|$color4|g" "$tmp_launcher_css"
                sed -i "s|{{surface}}|$color0|g" "$tmp_launcher_css"

                mv "$tmp_launcher_css" "$LAUNCHER_STYLE_OUTPUT"
                echo "Arch Launcher CSS written to $LAUNCHER_STYLE_OUTPUT"
            else
                echo "Error: Launcher template $LAUNCHER_STYLE_TEMPLATE not found."
            fi
        else
            echo "Warning: Color vars for C-Widgets Launcher not fully loaded. Skipping."
        fi

        # --- C-WIDGETS HYPER-CALENDAR START ---
        if [ -f "$HYPER_CALENDAR_STYLE_TEMPLATE" ]; then
            echo "Generating C-Widgets Hyper-Calendar CSS..."
            tmp_calendar_css=$(mktemp)
            cp "$HYPER_CALENDAR_STYLE_TEMPLATE" "$tmp_calendar_css"

            # Map wallust colors to the named colors in the CSS template
            # 'accent' -> color4 (main theme color)
            # 'surface' -> color0 (secondary background)
            # 'warning' -> color1 (red for destructive actions like the delete button)
            sed -i "s|%%BACKGROUND%%|$background|g" "$tmp_calendar_css"
            sed -i "s|%%FOREGROUND%%|$foreground|g" "$tmp_calendar_css"
            sed -i "s|%%ACCENT%%|$color4|g" "$tmp_calendar_css"
            sed -i "s|%%SURFACE%%|$color0|g" "$tmp_calendar_css"
            sed -i "s|%%WARNING%%|$color1|g" "$tmp_calendar_css"

            mv "$tmp_calendar_css" "$HYPER_CALENDAR_STYLE_OUTPUT"
            echo "Hyper-Calendar CSS written to $HYPER_CALENDAR_STYLE_OUTPUT"
        else
            echo "Error: Hyper-Calendar template $HYPER_CALENDAR_STYLE_TEMPLATE not found."
        fi
        # --- C-WIDGETS HYPER-CALENDAR END ---

                # --- C-WIDGETS CONTROL-CENTER START ---
        if [ -f "$CC_STYLE_TEMPLATE" ]; then
            # Check for the required wallust colors
            if [ -n "$background" ] && [ -n "$foreground" ] && [ -n "$color4" ] && [ -n "$color0" ] && [ -n "$color1" ]; then
                echo "Generating C-Widgets Control Center CSS..."
                tmp_cc_css=$(mktemp)
                cp "$CC_STYLE_TEMPLATE" "$tmp_cc_css"

                # Replace all the placeholders with their corresponding wallust colors
                sed -i "s|%%BACKGROUND%%|$background|g" "$tmp_cc_css"
                sed -i "s|%%FOREGROUND%%|$foreground|g" "$tmp_cc_css"
                sed -i "s|%%ACCENT%%|$color4|g" "$tmp_cc_css"
                sed -i "s|%%SURFACE%%|$color0|g" "$tmp_cc_css"
                sed -i "s|%%WARNING%%|$color1|g" "$tmp_cc_css"

                mv "$tmp_cc_css" "$CC_STYLE_OUTPUT"
                echo "Control Center CSS written to $CC_STYLE_OUTPUT"
            else
                echo "Warning: Color vars for Control Center not fully loaded. Skipping."
            fi
        else
            echo "Error: Control Center template $CC_STYLE_TEMPLATE not found."
        fi
        # --- C-WIDGETS CONTROL-CENTER END ---


         # --- C-WIDGETS SCHEDULE-WIDGET START ---
        if [ -f "$SCHEDULE_WIDGET_STYLE_TEMPLATE" ]; then
            # Check for the required wallust colors
            if [ -n "$background" ] && [ -n "$foreground" ] && [ -n "$color4" ] && [ -n "$color0" ] && [ -n "$color1" ]; then
                echo "Generating C-Widgets Schedule-Widget CSS..."
                tmp_schedule_css=$(mktemp)
                cp "$SCHEDULE_WIDGET_STYLE_TEMPLATE" "$tmp_schedule_css"

                # Map wallust colors to the named placeholders in the CSS template:
                # 'accent'  -> color4 (the main theme color)
                # 'surface' -> color0 (a secondary background shade)
                # 'warning' -> color1 (red, for destructive actions)
                sed -i "s|%%BACKGROUND%%|$background|g" "$tmp_schedule_css"
                sed -i "s|%%FOREGROUND%%|$foreground|g" "$tmp_schedule_css"
                sed -i "s|%%ACCENT%%|$color4|g" "$tmp_schedule_css"
                sed -i "s|%%SURFACE%%|$color0|g" "$tmp_schedule_css"
                sed -i "s|%%WARNING%%|$color1|g" "$tmp_schedule_css"

                mv "$tmp_schedule_css" "$SCHEDULE_WIDGET_STYLE_OUTPUT"
                echo "Schedule-Widget CSS written to $SCHEDULE_WIDGET_STYLE_OUTPUT"
            else
                echo "Warning: Color vars for Schedule-Widget not fully loaded. Skipping."
            fi
        else
            echo "Error: Schedule-Widget template $SCHEDULE_WIDGET_STYLE_TEMPLATE not found."
        fi
        # --- C-WIDGETS SCHEDULE-WIDGET END ---

         # --- C-WIDGETS SIDE-MPRIS-PLAYER START ---
        if [ -f "$MPRIS_PLAYER_STYLE_TEMPLATE" ]; then
            # Check for the required wallust colors
            if [ -n "$background" ] && [ -n "$foreground" ] && [ -n "$color4" ] && [ -n "$color0" ] && [ -n "$color1" ]; then
                echo "Generating C-Widgets Side-MPRIS-Player CSS..."
                tmp_mpris_css=$(mktemp)
                cp "$MPRIS_PLAYER_STYLE_TEMPLATE" "$tmp_mpris_css"

                # Map wallust colors to the named placeholders in the CSS template:
                # 'accent'  -> color4 (the main theme color)
                # 'surface' -> color0 (a secondary background shade)
                # 'warning' -> color1 (red, for alerts or destructive actions)
                sed -i "s|%%BACKGROUND%%|$background|g" "$tmp_mpris_css"
                sed -i "s|%%FOREGROUND%%|$foreground|g" "$tmp_mpris_css"
                sed -i "s|%%ACCENT%%|$color4|g" "$tmp_mpris_css"
                sed -i "s|%%SURFACE%%|$color0|g" "$tmp_mpris_css"
                sed -i "s|%%WARNING%%|$color1|g" "$tmp_mpris_css"

                mv "$tmp_mpris_css" "$MPRIS_PLAYER_STYLE_OUTPUT"
                echo "Side-MPRIS-Player CSS written to $MPRIS_PLAYER_STYLE_OUTPUT"
            else
                echo "Warning: Color vars for Side-MPRIS-Player not fully loaded. Skipping."
            fi
        else
            echo "Error: Side-MPRIS-Player template $MPRIS_PLAYER_STYLE_TEMPLATE not found."
        fi
        # --- C-WIDGETS SIDE-MPRIS-PLAYER END ---
        
        # --- C-WIDGETS LAUNCHER END ---

                   # --- SWAYNC START ---
        # --- SwayNC Colors ---
        if [ -n "$background" ] && [ -n "$foreground" ] && [ -n "$color0" ] && [ -n "$color1" ] && \
           [ -n "$color4" ] && [ -n "$color7" ] && [ -n "$color8" ] && [ -n "$color12" ] && \
           [ -n "$color13" ] && [ -n "$color14" ] && [ -n "$color15" ]; then
            echo "Generating SwayNC CSS from custom template..."
            if [ -f "$SWAYNC_STYLE_BASE" ]; then
                # Helper function to convert #RRGGBB to rgba(r,g,b,alpha)
                hex_to_rgba() {
                    local hex=${1#\#}
                    local alpha=$2
                    local r=$((16#${hex:0:2}))
                    local g=$((16#${hex:2:2}))
                    local b=$((16#${hex:4:2}))
                    echo "rgba($r, $g, $b, $alpha)"
                }

                # Create all the RGBA versions required by the template
                background_rgba_30=$(hex_to_rgba "$background" "0.3") # NEW
                background_rgba_60=$(hex_to_rgba "$background" "0.6")
                background_rgba_90=$(hex_to_rgba "$background" "0.9")
                color0_rgba_30=$(hex_to_rgba "$color0" "0.3")     # NEW
                color0_rgba_50=$(hex_to_rgba "$color0" "0.5")
                color1_rgba_50=$(hex_to_rgba "$color1" "0.5")
                color4_rgba_50=$(hex_to_rgba "$color4" "0.5")
                color7_rgba_50=$(hex_to_rgba "$color7" "0.5")
                color8_rgba_50=$(hex_to_rgba "$color8" "0.5")
                color12_rgba_50=$(hex_to_rgba "$color12" "0.5")
                color13_rgba_50=$(hex_to_rgba "$color13" "0.5")
                color14_rgba_50=$(hex_to_rgba "$color14" "0.5")
                
                tmp_swaync_css=$(mktemp)
                cp "$SWAYNC_STYLE_BASE" "$tmp_swaync_css"

                # Replace standard color placeholders
                sed -i "s|%%BACKGROUND%%|$background|g" "$tmp_swaync_css"
                sed -i "s|%%FOREGROUND%%|$foreground|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR0%%|$color0|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR1%%|$color1|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR4%%|$color4|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR7%%|$color7|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR15%%|$color15|g" "$tmp_swaync_css"

                # Replace RGBA placeholders
                sed -i "s|%%BACKGROUND_RGBA_30%%|$background_rgba_30|g" "$tmp_swaync_css" # NEW
                sed -i "s|%%BACKGROUND_RGBA_60%%|$background_rgba_60|g" "$tmp_swaync_css"
                sed -i "s|%%BACKGROUND_RGBA_90%%|$background_rgba_90|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR0_RGBA_30%%|$color0_rgba_30|g" "$tmp_swaync_css"         # NEW
                sed -i "s|%%COLOR0_RGBA_50%%|$color0_rgba_50|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR1_RGBA_50%%|$color1_rgba_50|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR4_RGBA_50%%|$color4_rgba_50|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR7_RGBA_50%%|$color7_rgba_50|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR8_RGBA_50%%|$color8_rgba_50|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR12_RGBA_50%%|$color12_rgba_50|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR13_RGBA_50%%|$color13_rgba_50|g" "$tmp_swaync_css"
                sed -i "s|%%COLOR14_RGBA_50%%|$color14_rgba_50|g" "$tmp_swaync_css"

                mv "$tmp_swaync_css" "$SWAYNC_STYLE_OUTPUT"
                echo "SwayNC CSS written to $SWAYNC_STYLE_OUTPUT"
            else
                echo "Error: SwayNC base style $SWAYNC_STYLE_BASE not found."
            fi
        else
            echo "Warning: SwayNC color vars not fully loaded from SCRIPT_COLORS_RAW. Skipping SwayNC."
        fi
        # --- SWAYNC END ---

     #    # --- Neovim Colorscheme ---
#         if [ -n "$background" ] && [ -n "$foreground" ] && [ -n "$cursor" ] && \
#            [ -n "$color0" ] && [ -n "$color1" ] && [ -n "$color2" ] && [ -n "$color3" ] && \
#            [ -n "$color4" ] && [ -n "$color5" ] && [ -n "$color6" ] && [ -n "$color7" ] && \
#            [ -n "$color8" ] && [ -n "$color9" ] && [ -n "$color10" ] && [ -n "$color11" ] && \
#            [ -n "$color12" ] && [ -n "$color13" ] && [ -n "$color14" ] && [ -n "$color15" ]; then
#             echo "Generating Neovim colorscheme..."
#             mkdir -p "$(dirname "$NVIM_THEME_OUTPUT")"
#             cat > "$NVIM_THEME_OUTPUT" << EOF
# -- wallust.lua
# -- Dynamically generated colorscheme from wallust colors
# -- This file is auto-generated! Do not edit directly.
# 
# local M = {}
# 
# function M.setup()
#     vim.cmd('highlight clear')
#     if vim.fn.exists('syntax_on') then
#         vim.cmd('syntax reset')
#     end
# 
#     vim.o.termguicolors = true
#     vim.g.colors_name = 'wallust'
# 
#     M.colors = {
#         background = "$background", foreground = "$foreground", cursor = "$cursor",
#         color0 = "$color0", color1 = "$color1", color2 = "$color2", color3 = "$color3",
#         color4 = "$color4", color5 = "$color5", color6 = "$color6", color7 = "$color7",
#         color8 = "$color8", color9 = "$color9", color10 = "$color10", color11 = "$color11",
#         color12 = "$color12", color13 = "$color13", color14 = "$color14", color15 = "$color15",
#     }
#     local colors = M.colors
# 
#     local hi = function(group, opts)
#         local cmd = "highlight " .. group
#         if opts.fg then cmd = cmd .. " guifg=" .. opts.fg end
#         if opts.bg then cmd = cmd .. " guibg=" .. opts.bg end
#         if opts.sp then cmd = cmd .. " guisp=" .. opts.sp end
#         if opts.style then cmd = cmd .. " gui=" .. opts.style end
#         vim.api.nvim_command(cmd)
#     end
# 
#     vim.g.terminal_color_0 = colors.color0; vim.g.terminal_color_1 = colors.color1
#     vim.g.terminal_color_2 = colors.color2; vim.g.terminal_color_3 = colors.color3
#     vim.g.terminal_color_4 = colors.color4; vim.g.terminal_color_5 = colors.color5
#     vim.g.terminal_color_6 = colors.color6; vim.g.terminal_color_7 = colors.color7
#     vim.g.terminal_color_8 = colors.color8; vim.g.terminal_color_9 = colors.color9
#     vim.g.terminal_color_10 = colors.color10; vim.g.terminal_color_11 = colors.color11
#     vim.g.terminal_color_12 = colors.color12; vim.g.terminal_color_13 = colors.color13
#     vim.g.terminal_color_14 = colors.color14; vim.g.terminal_color_15 = colors.color15
# 
#     hi("Normal", { fg = colors.foreground, bg = colors.color0 })
#     hi("NormalFloat", { fg = colors.foreground, bg = colors.color0 })
#     hi("ColorColumn", { bg = colors.background }); hi("Cursor", { fg = colors.background, bg = colors.cursor })
#     hi("CursorLine", { bg = colors.background }); hi("CursorColumn", { bg = colors.background })
#     hi("Directory", { fg = colors.color4 }); hi("DiffAdd", { bg = colors.color2, fg = colors.background })
#     hi("DiffChange", { bg = colors.color3, fg = colors.background }); hi("DiffDelete", { bg = colors.color1, fg = colors.background })
#     hi("LineNr", { fg = colors.color8, bg = colors.color0 }); hi("CursorLineNr", { fg = colors.color3, bg = colors.color0, style = "bold" })
#     hi("Pmenu", { fg = colors.foreground, bg = colors.color0 }); hi("PmenuSel", { fg = colors.foreground, bg = colors.color8 })
#     hi("StatusLine", { fg = colors.foreground, bg = colors.background }); hi("StatusLineNC", { fg = colors.color8, bg = colors.background })
#     hi("Visual", { bg = colors.color8 })
#     hi("Comment", { fg = colors.color8, style = "italic" }); hi("Constant", { fg = colors.color6 })
#     hi("String", { fg = colors.color4 }); hi("Identifier", { fg = colors.color5 })
#     hi("Function", { fg = colors.color4 }); hi("Statement", { fg = colors.color1 })
#     hi("Keyword", { fg = colors.color5, style = "bold" }); hi("Type", { fg = colors.color2 })
#     hi("PreProc", { fg = colors.color3 }); hi("Special", { fg = colors.color13 })
# end
# return M
# EOF
#             echo "Neovim colorscheme written to $NVIM_THEME_OUTPUT"
#         else
#             echo "Warning: Neovim color vars not fully loaded from SCRIPT_COLORS_RAW. Skipping Neovim."
#         fi


        # --- GLAVA SECTION START (COMMENTED OUT) ---
        # if [ -n "$color4" ] && [ -n "$background" ]; then # Check if wallust main accent and background colors are available
        #     echo "Generating GLava colors for specific shaders..."
        #
        #     if [ -f "$GLAVA_BARS_SHADER_FILE" ]; then
        #         echo "Updating colors in $GLAVA_BARS_SHADER_FILE..."
        #         sed -i "s|^#define COLOR .*|#define COLOR $color4|" "$GLAVA_BARS_SHADER_FILE"
        #         sed -i "s|^#define BGCOLOR .*|#define BGCOLOR $background|" "$GLAVA_BARS_SHADER_FILE"
        #         echo "Colors updated for bars.glsl."
        #     else
        #         echo "Warning: GLava bars shader $GLAVA_BARS_SHADER_FILE not found."
        #     fi
        #
        #     if [ -f "$GLAVA_RADIAL_SHADER_FILE" ]; then
        #         echo "Updating colors in $GLAVA_RADIAL_SHADER_FILE..."
        #         sed -i "s|^#define COLOR .*|#define COLOR $color4|" "$GLAVA_RADIAL_SHADER_FILE"
        #         sed -i "s|^#define BGCOLOR .*|#define BGCOLOR $background|" "$GLAVA_RADIAL_SHADER_FILE"
        #         echo "Colors updated for radial.glsl."
        #     else
        #         echo "Warning: GLava radial shader $GLAVA_RADIAL_SHADER_FILE not found."
        #     fi
        # else
        #     echo "Warning: Wallust color vars (color4, background) not loaded. Skipping GLava."
        # fi
        # --- GLAVA SECTION END ---

    else
        echo "Error: $SCRIPT_COLORS_RAW not found. Cannot generate scripted themes."
        send_notification -u critical "Script Error" "$SCRIPT_COLORS_RAW missing."
        return 1
    fi

    echo "Reloading applications..."
    hyprctl reload > /dev/null 2>&1 &
    sleep 0.5 # Give Hyprland a moment
    # --- WAYBAR (Hidden) ---
    # if pgrep -x "waybar" > /dev/null; then killall -SIGUSR2 waybar > /dev/null 2>&1; echo "Reloaded Waybar."; fi
        # if pgrep -x "kitty" > /dev/null; then killall -SIGUSR1 kitty > /dev/null 2>&1; echo "Reloaded Kitty."; fi
    # if pgrep -x "dunst" > /dev/null; then killall -SIGUSR2 dunst > /dev/null 2>&1; echo "Reloaded Dunst."; fi
    if pgrep -x "ironbar" > /dev/null; then
        echo "Ironbar style updated. It may reload automatically or require a manual signal."
    fi
    # --- GLAVA (Hidden) ---
    # if pgrep -x "glava" > /dev/null; then
    #     killall -SIGUSR1 glava > /dev/null 2>&1
    #     echo "Reloaded GLava."
    # fi
    # --- SWAYNC START ---
    if pgrep -x "swaync" > /dev/null; then swaync-client -rs > /dev/null 2>&1; echo "Reloaded SwayNC style."; fi
    # --- SWAYNC END ---
    echo "Application reload signals sent."

    # --- Reload Custom GTK Widgets (The Correct Way) ---
    #echo "Reloading custom GTK widgets..."
    
    # Cheatsheet doesn't need reloading as it's not a daemon.
    # It will load the new theme the next time it's launched.

    # For Arch Badge, we use the simple and reliable kill-and-restart method.
   # if pgrep -f "archbadge.py" > /dev/null; then
    #    echo "Restarting Arch Badge to apply new theme..."
     #   pkill -f "archbadge.py"
      #  sleep 0.2 # Give it a moment to die completely
    #fi
    # Start a new instance in the background using the correct path.
    #python "$HOME/.config/hypr/python/fetchapp/archbadge.py" &

# --- C-WIDGETS SIDEBAR (RELOAD) ---
# Restarts only if hypr-sidebar is currently running and killable.
#if pgrep -x "hypr-sidebar" > /dev/null; then
#    echo "Restarting C-Widgets Sidebar to apply new theme..."
#    if pkill -x "hypr-sidebar"; then
#        sleep 0.2 # Give it a moment to fully terminate
#        (
#            cd "$HOME/.config/hypr/C-widgets/sidebar/builddir" || exit
#            ./hypr-sidebar &
#        )
#    else
#        echo "Failed to kill hypr-sidebar; not restarting."
#    fi
#else
#    echo "hypr-sidebar not running; skipping restart."
#fi
# --- C-WIDGETS SIDEBAR END ---

    

    send_notification -i "$SELECTED_NEW_WALLPAPER_PATH" "✅ Theme Applied" "Your new theme is now active!"
    echo "Wallpaper and themes updated for $SELECTED_NEW_WALLPAPER_PATH."

    
    return 0
}

# --- Function to ensure swww-daemon is running ---
ensure_swww_daemon() {
    if ! pgrep -x "swww-daemon" > /dev/null; then
        echo "swww-daemon not running, attempting to initialize..."
        swww init >/dev/null 2>&1 && sleep 0.5
        if ! pgrep -x "swww-daemon" > /dev/null; then
            return 1 # Failure
        fi
        echo "swww-daemon initialized."
    fi
    return 0 # Success
}

# ---
# SCRIPT EXECUTION STARTS HERE
# ---

# NEW: Argument parsing for --mute flag
for arg in "$@"; do
    if [[ "$arg" == "--mute" ]]; then
        NOTIFICATIONS_MUTED=true
        echo "Notifications are muted."
    fi
done

# NEW: Lock file mechanism to prevent multiple instances
# The trap ensures the lock file is removed when the script exits, even on error.
trap 'rm -f "$LOCK_FILE"' EXIT
if [ -e "$LOCK_FILE" ]; then
    echo "Script is already running. Exiting."
    exit 1
else
    # Create the lock file to signal that the script is running
    touch "$LOCK_FILE"
fi
# ---

# --- Startup Mode ---
# This checks the first argument specifically.
if [[ "$1" == "--startup" ]]; then
    echo "Startup mode: Ensuring swww-daemon is running."
    if ! ensure_swww_daemon; then
        echo "Error: Failed to start swww-daemon on startup." >&2
        exit 1
    fi
    echo "swww-daemon is running. Startup script finished."
    exit 0
fi

# --- Check swww-daemon for all other modes ---
if ! ensure_swww_daemon; then
    send_notification -u critical "SWWW Error" "Failed to start swww-daemon for interactive mode."
    exit 1
fi

# --- Check for Wallpaper Directory ---
if [ ! -d "$WALLPAPER_DIR" ]; then
    send_notification -u critical "Wallpaper Script Error" "Directory '$WALLPAPER_DIR' not found."
    exit 1
fi

# --- Get Wallpaper List ---
if command -v fd &> /dev/null; then
    WALLPAPER_FILES=$(fd . "$WALLPAPER_DIR" -e png -e jpg -e jpeg -e gif -e webp --type f | sort)
else
    WALLPAPER_FILES=$(find "$WALLPAPER_DIR" -type f \( -iname '*.png' -o -iname '*.jpeg' -o -iname '*.jpg' -o -iname '*.gif' -o -iname '*.webp' \) | sort)
fi

if [ -z "$WALLPAPER_FILES" ]; then
    send_notification "Wallpaper Script Error" "No image files found in $WALLPAPER_DIR."
    exit 1
fi

# --- Cycling Mode (--next/--prev) ---
# This robustly checks if --next or --prev exists anywhere in the arguments
if [[ " $@ " =~ " --next " ]] || [[ " $@ " =~ " --prev " ]]; then
    echo "Cycling mode: Finding next/previous wallpaper."
    mapfile -t wallpaper_array < <(echo "$WALLPAPER_FILES")
    count=${#wallpaper_array[@]}
    current_wallpaper=$(swww query | head -n 1 | sed 's/.*: //')
    current_index=-1
    for i in "${!wallpaper_array[@]}"; do
        if [[ "${wallpaper_array[$i]}" == "$current_wallpaper" ]]; then
            current_index=$i
            break
        fi
    done
    if [[ $current_index -eq -1 ]]; then
        current_index=0
    fi
    if [[ " $@ " =~ " --next " ]]; then
        new_index=$(( (current_index + 1) % count ))
    else # --prev
        new_index=$(( (current_index - 1 + count) % count ))
    fi
    SELECTED_NEW_WALLPAPER_PATH="${wallpaper_array[$new_index]}"
    echo "Selected new wallpaper: $SELECTED_NEW_WALLPAPER_PATH"
    apply_theme_and_reload "$SELECTED_NEW_WALLPAPER_PATH"
    exit $?
fi

# --- Interactive Mode (using local cachy-selector) ---
echo "Interactive mode: Selecting new wallpaper with cachy-selector."

# The path to our custom selector, located in the same directory as this script.
# This is the robust way to call it, no matter where you run wallpaper.sh from.
SELECTOR_PATH="$(dirname "$0")/cachy-selector"

# We pipe the list of FULL PATHS directly into our app running in 'dmenu' mode.
# It will print the selected full path back to us.
SELECTED_NEW_WALLPAPER_PATH=$(echo "$WALLPAPER_FILES" | "$SELECTOR_PATH" dmenu)

if [ -z "$SELECTED_NEW_WALLPAPER_PATH" ]; then
    echo "No wallpaper selected."
    exit 0
fi

# We can directly use the output, since our app gives us the full path.
# The check below is still good practice.
if [ ! -f "$SELECTED_NEW_WALLPAPER_PATH" ]; then
    send_notification -u critical "Wallpaper Script Error" "Selector returned an invalid path: '$SELECTED_NEW_WALLPAPER_PATH'."
    exit 1
fi

apply_theme_and_reload "$SELECTED_NEW_WALLPAPER_PATH"
exit $?