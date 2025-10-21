> ## Are you lost, traveler?
>
> The custom C widgets and shell components showcased here have been moved to their own dedicated project: **[Aurora Shell](https://github.com/Spn4x/Aurora-Shell)**.
>
> This repository now contains only the dotfiles (configurations) used to integrate and theme those components.

---

# Spn4x's Hyprland Dotfiles

![Desktop Showcase](pics/allwidgets.png)

This repository contains the complete configuration for my personal Arch Linux setup, built on Hyprland. It's designed to integrate with **Aurora Shell**, a suite of custom widgets that form a cohesive and dynamically themed desktop environment.

## Philosophy

This setup is built around the idea of a "living desktop." The core principle is that a single action—changing the wallpaper—should trigger a cascade of updates that recolors and unifies every component of the UI. This repository contains the "glue" and theme files that bring that vision to life.

## Key Features

*   **Dynamic Theming:** Using **Wallust**, the entire 16-color palette is generated from the current wallpaper and applied across the system.
*   **Seamless Integration with Aurora Shell:** These dotfiles are tailored to use **[Aurora Shell](https://github.com/your-username/aurora-shell)**, a custom-built desktop shell written in C. The widgets showcased below (Control Center, Calendar, MPRIS Player, etc.) are part of that project.
*   **Aesthetic Cohesion:** Every component, from the Ironbar status bar to the SwayNC notification daemon, is themed using a consistent set of CSS templates, ensuring a pixel-perfect look.

---

## Showcase

*The components shown below are from the [Aurora Shell](https://github.com/your-username/aurora-shell) project, themed and configured by these dotfiles.*

| MPRIS Player with Synced Lyrics | Control Center |
| :---: | :---: |
| ![MPRIS Player Showcase](pics/mpris-preview.png) | ![Control Center Showcase](pics/control-center.png) |
| **Calendar** | **Dynamic Theme Example** |
| ![Calendar Showcase](pics/calendar-preview.png) | ![Theme Example 1](pics/changed-wallpaper-preview-1.png) |

---

## Core Components

This setup is built on a foundation of powerful open-source software.

*   **OS:** Arch Linux
*   **Window Manager:** [Hyprland](https://hyprland.org/)
*   **Custom Shell:** [Aurora Shell](https://github.com/your-username/aurora-shell)
*   **Theming Engine:** [Wallust](https://codeberg.org/explosion-mental/wallust)
*   **Status Bar:** [Ironbar](https://github.com/JakeStanger/ironbar)
*   **Wallpaper Daemon:** [swww](https://github.com/Horus645/swww)
*   **Notification Daemon:** [SwayNC](https://github.com/ErikReider/SwayNC)

---

## How to Use This Repository

⚠️ **CRITICAL WARNING:** This is a snapshot of my live, personal system. **Do not attempt to install these dotfiles directly.** Blindly applying these configurations will break your desktop environment.

This repository should be used for **inspiration and reference only.**

1.  **Browse the Code:** Explore the configuration files for ideas.
2.  **Isolate and Understand:** Read through the configuration for a specific component (e.g., `ironbar`, `hyprland`).
3.  **Adapt and Adopt:** Manually copy and **modify** the sections you find useful into your own personal dotfiles. You must adapt paths and settings to fit your own system.

**DO NOT run `manage.sh`.** This script is for my personal use only.

---

## License

This project is licensed under the [MIT License](LICENSE).