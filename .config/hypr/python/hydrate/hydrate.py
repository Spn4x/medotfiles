import gi
gi.require_version("Gtk", "3.0")
gi.require_version("GtkLayerShell", "0.1")

import cairo
from gi.repository import Gtk, GtkLayerShell, Gdk

class HydrateSidebar(Gtk.Window):
    MAX_GLASSES = 8.0
    BAR_WIDTH = 14
    GUIDE_LINE_WIDTH = 2
    BAR_CORNER_RADIUS = 7

    def __init__(self):
        Gtk.Window.__init__(self, title="Hydrate Sidebar")
        # --- ADJUSTED: Height cut down significantly ---
        self.set_default_size(80, 200)
        self.set_resizable(False)
        self.set_app_paintable(True)
        self.set_visual(self.get_screen().get_rgba_visual())

        # --- ADJUSTED: Positioning to BOTTOM LEFT ---
        GtkLayerShell.init_for_window(self)
        GtkLayerShell.set_layer(self, GtkLayerShell.Layer.TOP)
        # Set anchors for bottom-left corner
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.LEFT, True)
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.BOTTOM, True)
        # Set margins to lift it off the edges
        GtkLayerShell.set_margin(self, GtkLayerShell.Edge.LEFT, 10)
        GtkLayerShell.set_margin(self, GtkLayerShell.Edge.BOTTOM, 10)
        
        GtkLayerShell.set_keyboard_mode(self, GtkLayerShell.KeyboardMode.ON_DEMAND)

        self.current_glasses = 0.0
        self.build_ui()
        self.update_ui()

    def build_ui(self):
        container = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        container.set_margin_top(20)
        container.set_margin_bottom(20)
        self.add(container)

        self.drawing_area = Gtk.DrawingArea()
        self.drawing_area.add_events(Gdk.EventMask.SCROLL_MASK)
        self.drawing_area.connect("scroll-event", self.on_scroll)
        self.drawing_area.connect("draw", self.on_draw_area)

        self.label = Gtk.Label(label="...")

        container.pack_start(self.drawing_area, True, True, 0)
        container.pack_start(self.label, False, False, 0)

    def on_scroll(self, widget, event):
        # --- ADJUSTED: Sensitivity is now even denser ---
        if event.direction == Gdk.ScrollDirection.UP:
            self.current_glasses += 0.1
        elif event.direction == Gdk.ScrollDirection.DOWN:
            self.current_glasses -= 0.1

        self.current_glasses = max(0.0, min(self.MAX_GLASSES, self.current_glasses))
        self.update_ui()
        return True

    def draw_rounded_rectangle(self, cr, x, y, width, height, radius):
        """Helper function to draw a rectangle with rounded corners."""
        cr.new_sub_path()
        cr.arc(x + radius, y + radius, radius, 3.14159, 1.5 * 3.14159)
        cr.arc(x + width - radius, y + radius, radius, 1.5 * 3.14159, 2 * 3.14159)
        cr.arc(x + width - radius, y + height - radius, radius, 0, 0.5 * 3.14159)
        cr.arc(x + radius, y + height - radius, radius, 0.5 * 3.14159, 1 * 3.14159)
        cr.close_path()

    def on_draw_area(self, widget, cr):
        """Custom-draws a more stylish progress bar."""
        width = widget.get_allocated_width()
        height = widget.get_allocated_height()
        center_x = width / 2.0

        cr.set_source_rgba(0.1, 0.1, 0.15, 0.6)
        self.draw_rounded_rectangle(cr, center_x - self.BAR_WIDTH / 2, 0, self.BAR_WIDTH, height, self.BAR_CORNER_RADIUS)
        cr.fill()

        fill_fraction = self.current_glasses / self.MAX_GLASSES
        if fill_fraction <= 0:
            return

        fill_height = height * fill_fraction
        fill_y = height - fill_height

        gradient = cairo.LinearGradient(0, fill_y, 0, height)
        gradient.add_color_stop_rgba(0.0, 0.46, 0.80, 0.95, 1.0)
        gradient.add_color_stop_rgba(1.0, 0.20, 0.60, 0.86, 1.0)

        cr.set_source(gradient)
        self.draw_rounded_rectangle(cr, center_x - self.BAR_WIDTH / 2, fill_y, self.BAR_WIDTH, fill_height, self.BAR_CORNER_RADIUS)
        cr.fill()

    def update_ui(self):
        """Updates the label and tells the drawing area to repaint."""
        if self.current_glasses >= self.MAX_GLASSES:
            self.label.set_text("✅ Full!")
        else:
            self.label.set_text(f"💧 {self.current_glasses:.1f}")
        
        self.drawing_area.queue_draw()

def apply_css():
    """CSS to style the window and label."""
    css = b"""
    window {
        background-color: rgba(20, 20, 30, 0.8);
        border-radius: 15px;
        border: 1px solid rgba(255, 255, 255, 0.1);
    }
    
    label {
        color: #ddeeff;
        font-weight: bold;
        font-size: 14px;
        text-shadow: 1px 1px 2px black;
    }
    """
    css_provider = Gtk.CssProvider()
    css_provider.load_from_data(css)
    screen = Gdk.Screen.get_default()
    style_context = Gtk.StyleContext()
    style_context.add_provider_for_screen(screen, css_provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

def main():
    apply_css()
    win = HydrateSidebar()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()

if __name__ == "__main__":
    main()