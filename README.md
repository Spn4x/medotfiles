# Spn4x's Hyprland Dotfiles

![Desktop Showcase](pics/image.png)

A fully automated and cohesive desktop experience built on Hyprland, where the entire UI is dynamically themed from the wallpaper. This repository contains the complete configuration for my personal Arch Linux setup, including a suite of custom-built C widgets that form a complete desktop environment.

---

##  philosophy

This setup is built around the idea of a "living desktop"—an environment that is not only visually stunning but also deeply integrated and automated. The core principle is that a single action, changing the wallpaper, should trigger a cascade of updates that recolor and unify every component of the UI, from the window borders to the terminal to custom application widgets.

This is a "backyard mechanic's" approach to the desktop: if a tool didn't exist or didn't fit the aesthetic, I built it.

## Key Features

*   **Dynamic Theming:** Using **Wallust**, the entire 16-color palette is generated from the current wallpaper. A central management script then applies this theme to every configured application.
*   **Custom C Widgets:** The core of the experience. These are not just display items; they are fully-functional applications built from scratch to integrate seamlessly with the desktop:
    *   **Control Center:** Manages Wi-Fi, Bluetooth, audio sinks, brightness, and volume.
    *   **Hyper-Calendar & Schedule Widget:** A full calendar with CRUD (Create, Read, Update, Delete) functionality for managing events and schedules.
    *   **Side-MPRIS-Player:** A media player widget that displays metadata and features perfectly **synced lyrics** for music playing in any MPRIS-compatible player (including browsers).
*   **Centralized Management:** A single shell script (`manage.sh`) handles the installation of dotfiles via symbolic linking.
*   **Aesthetic Cohesion:** Every component, from the Ironbar status bar to the SwayNC notification daemon, is themed using a consistent set of CSS templates, ensuring a pixel-perfect, unified look and feel.

---

## Showcase

Here are a few videos demonstrating the setup in action.

| Synced Lyrics Showcase | Dynamic Theming in Action |
| :---: | :---: |
| [Video Link or GIF](vids/vid1.mp4) | [Video Link or GIF](vids/vid2.mp4) |

### Gallery

Detailed views of the custom widgets and UI elements.

| MPRIS Player & Lyrics | Calendar & Schedule |
| :---: | :---: |
| ![MPRIS Player with Synced Lyrics](pics/image2.png) | ![Calendar Widget](pics/image3.png) |
| **Control Center** | **Full Desktop Layout** |
| ![Control Center Widget](pics/image4.png) | ![Full Desktop View](pics/image.png) |

---

## Core Components

This setup is built on a foundation of powerful and flexible open-source software.

*   **OS:** Arch Linux
*   **Window Manager:** [Hyprland](https://hyprland.org/)
*   **Theming Engine:** [Wallust](https://github.com/wallust-project/wallust)
*   **Status Bar:** [Ironbar](https://github.com/JakeStanger/ironbar)
*   **Wallpaper Daemon:** [swww](https://github.com/Horus645/swww)
*   **Terminal:** [Kitty](https://sw.kovidgoyal.net/kitty/)
*   **Notification Daemon:** [SwayNC](https://github.com/ErikReider/SwayNC)
*   **Custom Widgets:** Written in C using the GTK library.

---

## Installation

⚠️ **WARNING:** These are my personal dotfiles. They are highly customized for my specific hardware, software, and workflow. **Do not clone this repository and run the scripts blindly.** You will almost certainly break your system.

This repository is intended to be used for inspiration. Feel free to browse the configurations, copy snippets you find useful, and learn from the structure.

---

## Acknowledgments

This project would not be possible without the incredible work of the developers behind Hyprland, Wallust, and the entire Linux FOSS community. Thank you for providing the tools that allow for such deep and meaningful customization.

---

## License

This project is licensed under the [MIT License](LICENSE). Feel free to use and modify the code as you see fit.