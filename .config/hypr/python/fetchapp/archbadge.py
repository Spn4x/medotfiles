import gi
import os
import psutil
import distro

gi.require_version("Gtk", "3.0")
gi.require_version("GtkLayerShell", "0.1")
from gi.repository import Gtk, GtkLayerShell, Gdk, GLib

# --- USER CONFIGURATION ---
FONT_SIZE = "10pt"
MARGIN_FROM_BOTTOM = 6
UPDATE_INTERVAL = 60000

class UptimeWidget(Gtk.Window):
    def __init__(self):
        super().__init__()
        self.setup_window()
        self.setup_layout()
        
        self.update_info()
        GLib.timeout_add(UPDATE_INTERVAL, self.update_info)
        
    def setup_window(self):
        GtkLayerShell.init_for_window(self)
        GtkLayerShell.set_layer(self, GtkLayerShell.Layer.BOTTOM)
        GtkLayerShell.set_keyboard_mode(self, GtkLayerShell.KeyboardMode.NONE)
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.BOTTOM, True)
        GtkLayerShell.set_margin(self, GtkLayerShell.Edge.BOTTOM, MARGIN_FROM_BOTTOM)
        
        self.set_position(Gtk.WindowPosition.CENTER)
        self.set_visual(self.get_screen().get_rgba_visual())
        self.set_app_paintable(True)
        self.set_decorated(False)
        self.set_resizable(False)

    def setup_layout(self):
        main_container = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        main_container.get_style_context().add_class("main-container")
        
        self.info_label = Gtk.Label(label="Loading info...")
        self.info_label.get_style_context().add_class("info-label")
        
        main_container.pack_start(self.info_label, True, True, 0)
        self.add(main_container)

    def update_info(self):
        os_name = distro.name()
        uptime_str = os.popen("uptime -p").read().strip().replace("up ", "")
        self.info_label.set_text(f"{os_name}  ·  Uptime: {uptime_str}")
        return True

def load_css():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # --- THE FIX IS HERE ---
    # The script now loads the correct CSS file name.
    css_file_path = os.path.join(script_dir, "archbadge.css")
    print(f"CSS path: {css_file_path} — Exists? {os.path.exists(css_file_path)}")

    file_provider = Gtk.CssProvider()
    try:
        file_provider.load_from_path(css_file_path)
        screen = Gdk.Screen.get_default()
        Gtk.StyleContext.add_provider_for_screen(screen, file_provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
    except Exception as e:
        print(f"Error loading CSS file: {e}")

    font_css = f".info-label {{ font-size: {FONT_SIZE}; }}".encode()
    font_provider = Gtk.CssProvider()
    font_provider.load_from_data(font_css)
    Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), font_provider, Gtk.STYLE_PROVIDER_PRIORITY_USER)

if __name__ == "__main__":
    load_css()
    win = UptimeWidget()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()