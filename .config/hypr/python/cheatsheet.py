import gi
import os

gi.require_version("Gtk", "3.0")
gi.require_version("GtkLayerShell", "0.1")
from gi.repository import Gtk, GtkLayerShell, Gdk, GLib

# --- USER CONFIGURATION ---
WINDOW_WIDTH = 900
WINDOW_HEIGHT = 500
NUM_COLUMNS = 3
FONT_SIZE = "11pt"

class CheatWindow(Gtk.Window):
    def __init__(self):
        super().__init__()
        self.setup_window()
        self.setup_layout()
        self.connect("key-press-event", self.on_key_press)

    def setup_window(self):
        GtkLayerShell.init_for_window(self)
        GtkLayerShell.set_layer(self, GtkLayerShell.Layer.TOP)
        GtkLayerShell.set_keyboard_mode(self, GtkLayerShell.KeyboardMode.ON_DEMAND)
        self.set_position(Gtk.WindowPosition.CENTER)
        self.set_default_size(WINDOW_WIDTH, WINDOW_HEIGHT)
        self.set_visual(self.get_screen().get_rgba_visual())
        self.set_title("Keybinding Cheat Sheet")
        self.set_decorated(False)
        self.set_resizable(False)
        self.set_app_paintable(True)

    def setup_layout(self):
        main_container = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        main_container.get_style_context().add_class("main-container")
        main_grid = Gtk.Grid()
        main_grid.set_column_spacing(30)
        main_grid.set_margin_top(20)
        main_grid.set_margin_bottom(20)
        main_grid.set_margin_start(25)
        main_grid.set_margin_end(25)
        self.build_columns(main_grid)
        main_container.pack_start(main_grid, True, True, 0)
        self.add(main_container)

    def build_columns(self, main_grid):
        categories = self.parse_categories_from_file()
        if not categories:
            main_grid.add(Gtk.Label(label="No categories found in keys.conf"))
            return

        columns = [Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20) for _ in range(NUM_COLUMNS)]
        for i, category_data in enumerate(categories):
            target_column_box = columns[i % NUM_COLUMNS]
            category_widget = self.create_category_widget(category_data)
            target_column_box.pack_start(category_widget, False, False, 0)
        for i, column_box in enumerate(columns):
            main_grid.attach(column_box, i, 0, 1, 1)

    def parse_categories_from_file(self):
        script_dir = os.path.dirname(os.path.abspath(__file__))
        keys_file_path = os.path.join(script_dir, "keys.conf")
        if not os.path.exists(keys_file_path): return []
        
        categories = []
        current_category = None
        
        with open(keys_file_path, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"): continue
                
                if "=" not in line:
                    if current_category: categories.append(current_category)
                    current_category = [line]
                elif current_category:
                    current_category.append(line)
        
        if current_category: categories.append(current_category)
        return categories

    def create_category_widget(self, category_data):
        header_text = category_data[0]
        bindings = category_data[1:]
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        box.get_style_context().add_class("category-box")
        header_label = Gtk.Label(label=header_text)
        header_label.set_halign(Gtk.Align.START)
        header_label.get_style_context().add_class("category-header")
        box.pack_start(header_label, False, False, 0)
        grid = Gtk.Grid()
        grid.set_column_spacing(20)
        grid.set_row_spacing(8)
        for i, line in enumerate(bindings):
            key, description = [part.strip() for part in line.split("=", 1)]
            key_label = Gtk.Label(label=key)
            key_label.set_halign(Gtk.Align.END)
            key_label.get_style_context().add_class("key-label")
            desc_label = Gtk.Label(label=description)
            desc_label.set_halign(Gtk.Align.START)
            desc_label.get_style_context().add_class("desc-label")
            grid.attach(key_label, 0, i, 1, 1)
            grid.attach(desc_label, 1, i, 1, 1)
        box.pack_start(grid, True, True, 0)
        return box

    def on_key_press(self, _, event):
        if event.keyval == Gdk.KEY_Escape:
            Gtk.main_quit()

def load_css():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    css_file_path = os.path.join(script_dir, "cheatsheet.css")
    
    file_provider = Gtk.CssProvider()
    try:
        file_provider.load_from_path(css_file_path)
        screen = Gdk.Screen.get_default()
        Gtk.StyleContext.add_provider_for_screen(
            screen, file_provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
    except Exception as e:
        print(f"Error loading CSS file: {e}")

    font_css = f"""
        .key-label, .desc-label {{
            font-size: {FONT_SIZE};
        }}
        .category-header {{
            font-size: {float(FONT_SIZE.replace("pt", "")) + 1}pt;
        }}
    """.encode()
    font_provider = Gtk.CssProvider()
    font_provider.load_from_data(font_css)
    Gtk.StyleContext.add_provider_for_screen(
        Gdk.Screen.get_default(), font_provider, Gtk.STYLE_PROVIDER_PRIORITY_USER
    )

if __name__ == "__main__":
    load_css()
    win = CheatWindow()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()
