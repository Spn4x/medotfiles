#!/bin/bash

# --- Configuration ---
# Set the full path to your installed SSH askpass program
# Based on your `pacman -Ql x11-ssh-askpass` output
ASKPASS_PROGRAM="/usr/lib/ssh/x11-ssh-askpass"

# +++ WALLUST INTEGRATION: Define path to Wofi themed stylesheet +++
WOFI_STYLE_GENERATED="$HOME/.config/wofi/style-wallust-generated.css"
# ---

# --- Sanity Check for Askpass Program ---
if [ ! -x "$ASKPASS_PROGRAM" ]; then
    if command -v notify-send &> /dev/null; then
        notify-send -u critical "Wofi Boot Menu Error" "Askpass program '$ASKPASS_PROGRAM' not found or not executable. Please check the path in the script."
    else
        echo "Critical Error: Askpass program '$ASKPASS_PROGRAM' not found or not executable." >&2
    fi
    exit 1
fi

# --- Get current boot entries from efibootmgr ---
entries=$(SUDO_ASKPASS="$ASKPASS_PROGRAM" sudo -A efibootmgr 2>/dev/null | awk '
    /^Boot[0-9A-F]{4}\*/ {
        bootnum = substr($1, 5, 4);
        label = "";
        for (i = 2; i <= NF; i++) {
            label = label $i " ";
        }
        gsub(/ $/, "", label);
        if (label !~ /Setup/ && \
            label !~ /Boot Menu/ && \
            label !~ /Diagnostic Splash Screen/ && \
            label !~ /Lenovo Diagnostics/ && \
            label !~ /Startup Interrupt Menu/ && \
            label !~ /Rescue and Recovery/ && \
            label !~ /MEBx Hot Key/ && \
            label !~ /Linux-Firmware-Updater/ && \
            label !~ /USB CD/ && \
            label !~ /USB FDD/ && \
            label !~ /USB HDD/ && \
            label !~ /NVMe[0-9]/ && \
            label !~ /ATA HDD[0-9]/ && \
            label !~ /PCI LAN/ && \
            label !~ /Other CD/ && \
            label !~ /Other HDD/ && \
            label !~ /IDER BOOT CDROM/ && \
            label !~ /IDER BOOT Floppy/ && \
            label !~ /ATAPI CD/) {
                print bootnum " " label;
        }
    }')

# --- Check if entries were successfully retrieved ---
if [ -z "$entries" ]; then
    if ! SUDO_ASKPASS="$ASKPASS_PROGRAM" sudo -A -n true 2>/dev/null; then
        notify-send "Wofi Boot Menu" "Sudo access denied or password entry cancelled."
    else
        notify-send "Wofi Boot Menu" "No bootable entries found or an error occurred with efibootmgr."
    fi
    exit 1
fi

# --- Present options in Wofi ---
# OLD Wofi call:
# chosen_line=$(echo -e "$entries" | wofi --dmenu --ignore-case --prompt "Select Boot Option:")
# +++ WALLUST INTEGRATION: Add --style flag to Wofi call +++
chosen_line=$(echo -e "$entries" | wofi --dmenu --ignore-case --prompt "Select Boot Option:" --style "$WOFI_STYLE_GENERATED")
# ---

# If nothing chosen (user pressed Esc in Wofi), exit gracefully
if [ -z "$chosen_line" ]; then
    exit 0
fi

# --- Parse the chosen line to get BootNum and Label ---
chosen_bootnum=$(echo "$chosen_line" | awk '{print $1}')
chosen_label=$(echo "$chosen_line" | sed "s/^$chosen_bootnum //")

# --- Confirmation dialog using Wofi ---
# OLD Wofi call:
# confirm=$(echo -e "No\nYes" | wofi --dmenu --prompt "Boot to '$chosen_label' (ID: $chosen_bootnum) on next restart?")
# +++ WALLUST INTEGRATION: Add --style flag to Wofi call +++
confirm=$(echo -e "No\nYes" | wofi --dmenu --prompt "Boot to '$chosen_label' (ID: $chosen_bootnum) on next restart?" --style "$WOFI_STYLE_GENERATED")
# ---

if [ "$confirm" == "Yes" ]; then
    if SUDO_ASKPASS="$ASKPASS_PROGRAM" sudo -A efibootmgr --bootnext "$chosen_bootnum"; then
        notify-send "Wofi Boot Menu" "Next boot set to '$chosen_label'."

        # Ask to reboot, shutdown, or cancel
        # OLD Wofi call:
        # action=$(echo -e "Cancel\nShutdown\nReboot" | wofi --dmenu --prompt "Action?")
        # +++ WALLUST INTEGRATION: Add --style flag to Wofi call +++
        action=$(echo -e "Cancel\nShutdown\nReboot" | wofi --dmenu --prompt "Action?" --style "$WOFI_STYLE_GENERATED")
        # ---
        case "$action" in
            Reboot)
                SUDO_ASKPASS="$ASKPASS_PROGRAM" sudo -A systemctl reboot
                ;;
            Shutdown)
                SUDO_ASKPASS="$ASKPASS_PROGRAM" sudo -A systemctl poweroff
                ;;
            *)
                notify-send "Wofi Boot Menu" "Bootnext set. Reboot manually when ready."
                ;;
        esac
    else
        notify-send -u critical "Wofi Boot Menu" "Error setting bootnext for '$chosen_label'. (Password incorrect or action cancelled?)"
    fi
else
    notify-send "Wofi Boot Menu" "Boot selection cancelled."
fi

exit 0