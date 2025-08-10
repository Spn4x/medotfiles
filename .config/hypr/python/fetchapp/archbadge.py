#!/usr/bin/env python3

import gi
import os
import psutil
import distro

# Use Gio for file monitoring
gi.require_version("Gtk", "3.0")
gi.require_version("GtkLayerShell", "0.1")
from gi.repository import Gtk, GtkLayerShell, Gdk, GLib, Gio

# --- USER CONFIGURATION ---
FONT_SIZE = "10pt"
MARGIN_FROM_BOTTOM = 6
UPDATE_INTERVAL = 60000
CSS_FILE_NAME = "archbadge.css"

class UptimeWidget(Gtk.Window):
    """
    A Gtk.Window that displays system information and supports live CSS hot-reloading.
    """
    def __init__(self):
        super().__init__()
        
        # --- Attributes for Hot Reload ---
        # Holds the provider for the dynamic CSS file, so we can remove/replace it
        self.css_file_provider = None
        # Holds the file monitor object
        self.css_monitor = None
        
        self.setup_window()
        self.setup_layout()

        # Initial style setup
        self.load_static_styles()
        self.load_dynamic_styles()
        self.setup_css_hot_reload()
        
        # Update the text content
        self.update_info()
        GLib.timeout_add(UPDATE_INTERVAL, self.update_info)
        
    def setup_window(self):
        """Configures the window properties using GtkLayerShell."""
        GtkLayerShell.init_for_window(self)
        GtkLayerShell.set_layer(self, GtkLayerShell.Layer.BOTTOM)
        GtkLayerShell.set_keyboard_mode(self, GtkLayerShell.KeyboardMode.NONE)
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.BOTTOM, True)
        GtkLayerShell.set_margin(self, GtkLayerShell.Edge.BOTTOM, MARGIN_FROM_BOTTOM)
        GtkLayerShell.set_exclusive_zone(self, -1)
        
        self.set_position(Gtk.WindowPosition.CENTER)
        self.set_visual(self.get_screen().get_rgba_visual())
        self.set_app_paintable(True)
        self.set_decorated(False)
        self.set_resizable(False)

    def setup_layout(self):
        """Creates and arranges the widgets inside the window."""
        main_container = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        main_container.get_style_context().add_class("main-container")
        
        self.info_label = Gtk.Label(label="Loading info...")
        self.info_label.get_style_context().add_class("info-label")
        
        main_container.pack_start(self.info_label, True, True, 0)
        self.add(main_container)

    def update_info(self):
        """Fetches and updates the OS and uptime information."""
        try:
            os_name = distro.name()
            uptime_str = os.popen("uptime -p").read().strip().replace("up ", "")
            self.info_label.set_text(f"{os_name}  ·  Uptime: {uptime_str}")
        except Exception as e:
            print(f"Failed to update info: {e}")
            self.info_label.set_text("Error fetching info")
        return True # Necessary for GLib.timeout_add to continue

    def load_static_styles(self):
        """
        Loads CSS styles that do not change at runtime, like the font size.
        This is done only once.
        """
        font_css = f".info-label {{ font-size: {FONT_SIZE}; }}".encode()
        font_provider = Gtk.CssProvider()
        font_provider.load_from_data(font_css)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(),
            font_provider, 
            Gtk.STYLE_PROVIDER_PRIORITY_USER
        )

    def load_dynamic_styles(self):
        """
        Loads (or reloads) the CSS from the external file.
        This function is the core of the hot-reload mechanism. It ensures the old
        CSS provider is removed before applying the new one.
        """
        screen = Gdk.Screen.get_default()
        
        # --- THIS IS THE KEY TO HOT-RELOAD ---
        # If a provider for the file already exists, we remove it first.
        if self.css_file_provider:
            Gtk.StyleContext.remove_provider_for_screen(screen, self.css_file_provider)

        script_dir = os.path.dirname(os.path.abspath(__file__))
        css_file_path = os.path.join(script_dir, CSS_FILE_NAME)

        if not os.path.exists(css_file_path):
            print(f"Warning: CSS file not found at {css_file_path}")
            return
            
        try:
            self.css_file_provider = Gtk.CssProvider()
            self.css_file_provider.load_from_path(css_file_path)
            Gtk.StyleContext.add_provider_for_screen(
                screen, 
                self.css_file_provider,
                Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
            )
        except Exception as e:
            print(f"Error loading CSS file: {e}")

    def setup_css_hot_reload(self):
        """
        Creates a Gio.FileMonitor to watch the CSS file for changes.
        """
        script_dir = os.path.dirname(os.path.abspath(__file__))
        css_file_path = os.path.join(script_dir, CSS_FILE_NAME)
        
        css_file = Gio.File.new_for_path(css_file_path)
        self.css_monitor = css_file.monitor_file(Gio.FileMonitorFlags.NONE, None)
        self.css_monitor.connect("changed", self.on_css_changed)
        print(f"[Hot Reload] Monitoring {CSS_FILE_NAME} for changes...")

    def on_css_changed(self, monitor, file, other_file, event_type):
        """
        Callback function executed when the file monitor detects a change.
        """
        # We only care when the file is done changing.
        if event_type == Gio.FileMonitorEvent.CHANGES_DONE_HINT:
            print(f"[Hot Reload] {CSS_FILE_NAME} changed. Reloading styles...")
            # Reloading must be done on the main GTK thread, GLib.idle_add ensures this.
            GLib.idle_add(self.load_dynamic_styles)


if __name__ == "__main__":
    win = UptimeWidget()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()