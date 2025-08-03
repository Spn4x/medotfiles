import gi
import os
import subprocess
import json
import re
from datetime import datetime

gi.require_version("Gtk", "3.0")
gi.require_version("GtkLayerShell", "0.1")
from gi.repository import Gtk, GtkLayerShell, Gdk, GLib

# --- Constants ---
WINDOW_WIDTH = 960
WINDOW_HEIGHT = 520
UPDATE_INTERVAL_SECONDS = 10

FAN_CONTROL_FILE = os.environ.get("SIGMA_FAN_FILE", "/proc/acpi/ibm/fan")
WELLBEING_DATA_FILE = os.environ.get("SIGMA_WELLBEING_FILE", "/tmp/sigma_dashboard_wellbeing.txt")


class SigmaFanDashboard(Gtk.Window):
    def __init__(self):
        super().__init__()
        self.control_buttons = {}
        self.ansi_escape = re.compile(r'\x1b\[[0-9;]*[mGKHF]')
        self.collapsed = False

        # New widgets for Tier 1 features
        self.cpu_usage_bar = None
        self.ram_usage_bar = None
        # State for calculating CPU usage delta
        self.last_cpu_info = {'total': 0, 'idle': 0}

        self.setup_window()
        self.build_ui()
        GLib.timeout_add_seconds(UPDATE_INTERVAL_SECONDS, self.update_info)
        self.connect("key-press-event", self.on_key_press)

    def setup_window(self):
        GtkLayerShell.init_for_window(self)
        GtkLayerShell.set_layer(self, GtkLayerShell.Layer.TOP)
        GtkLayerShell.set_keyboard_mode(self, GtkLayerShell.KeyboardMode.ON_DEMAND)
        self.set_default_size(WINDOW_WIDTH, WINDOW_HEIGHT)
        self.set_visual(self.get_screen().get_rgba_visual())
        self.set_app_paintable(True)
        self.set_decorated(False)
        self.set_resizable(False)
        self.set_title("Sigma Dashboard")

    def build_ui(self):
        main_hbox = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=20)
        main_hbox.get_style_context().add_class("main-container")
        self.add(main_hbox)

        self.left_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20)
        main_hbox.pack_start(self.left_box, True, True, 0)
        self.build_left_panel()

        divider = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        main_hbox.pack_start(divider, False, False, 0)

        self.right_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        main_hbox.pack_start(self.right_box, True, True, 0)
        self.build_right_panel()

        GLib.idle_add(self.update_info)

    def build_left_panel(self):
        title = Gtk.Label(label="SYSTEM CONTROL")
        title.get_style_context().add_class("category-header")
        self.left_box.pack_start(title, False, False, 0)

        info_grid = Gtk.Grid(column_spacing=20, row_spacing=5)
        self.left_box.pack_start(info_grid, False, False, 0)
        self.temp_value_label = Gtk.Label(label="--°C")
        self.temp_value_label.get_style_context().add_class("info-value")
        self.fan_value_label = Gtk.Label(label="---- RPM")
        self.fan_value_label.get_style_context().add_class("info-value")
        temp_header = Gtk.Label(label="CPU Temp")
        temp_header.get_style_context().add_class("key-label")
        fan_header = Gtk.Label(label="Fan Speed")
        fan_header.get_style_context().add_class("key-label")
        for widget in [self.temp_value_label, self.fan_value_label, temp_header, fan_header]:
            widget.set_halign(Gtk.Align.CENTER)
            widget.set_hexpand(True)
        info_grid.attach(temp_header, 0, 0, 1, 1)
        info_grid.attach(self.temp_value_label, 0, 1, 1, 1)
        info_grid.attach(fan_header, 1, 0, 1, 1)
        info_grid.attach(self.fan_value_label, 1, 1, 1, 1)

        ctl_header = Gtk.Label(label="FAN CONTROL")
        ctl_header.get_style_context().add_class("key-label")
        ctl_header.set_halign(Gtk.Align.START)
        self.left_box.pack_start(ctl_header, False, False, 0)

        ctl_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.left_box.pack_start(ctl_box, False, False, 0)

        mode_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        mode_icons = {"auto": "view-refresh-symbolic", "full-speed": "weather-windy-symbolic"}
        for mode in ["auto", "full-speed"]:
            btn = Gtk.Button()
            btn.get_style_context().add_class("mode-button")
            hbox = Gtk.Box(spacing=6)
            icon = Gtk.Image.new_from_icon_name(mode_icons[mode], Gtk.IconSize.BUTTON)
            label = Gtk.Label(label=mode.replace('-', ' ').title())
            hbox.pack_start(icon, False, False, 0)
            hbox.pack_start(label, False, False, 0)
            btn.add(hbox)
            btn.connect("clicked", self.on_set_mode, mode)
            self.control_buttons[mode] = btn
            mode_box.pack_start(btn, True, True, 0)
        ctl_box.pack_start(mode_box, False, False, 0)

        grid = Gtk.Grid(column_homogeneous=True, row_spacing=10, column_spacing=10)
        grid.set_margin_top(5)
        for i in range(8):
            lvl = str(i)
            b = Gtk.Button(label=lvl)
            b.get_style_context().add_class("level-button")
            b.set_tooltip_text(f"Set fan to level {lvl}")
            b.connect("clicked", self.on_set_mode, lvl)
            self.control_buttons[lvl] = b
            grid.attach(b, i % 4, i // 4, 1, 1)
        ctl_box.pack_start(grid, False, False, 0)

        # --- NEW: Resource Monitoring Section ---
        res_header = Gtk.Label(label="RESOURCES")
        res_header.get_style_context().add_class("key-label")
        res_header.set_halign(Gtk.Align.START)
        res_header.set_margin_top(15) # Add space above this section
        self.left_box.pack_start(res_header, False, False, 0)

        res_grid = Gtk.Grid(column_spacing=10, row_spacing=8)
        res_grid.set_column_homogeneous(False) # Allow label column to be smaller
        self.left_box.pack_start(res_grid, False, False, 0)

        # CPU Row
        cpu_label = Gtk.Label(label="CPU", xalign=0)
        self.cpu_usage_bar = Gtk.ProgressBar()
        self.cpu_usage_bar.set_show_text(True)
        self.cpu_usage_bar.get_style_context().add_class("resource-progressbar")
        self.cpu_usage_bar.set_hexpand(True)
        res_grid.attach(cpu_label, 0, 0, 1, 1)
        res_grid.attach(self.cpu_usage_bar, 1, 0, 1, 1)

        # RAM Row
        ram_label = Gtk.Label(label="RAM", xalign=0)
        self.ram_usage_bar = Gtk.ProgressBar()
        self.ram_usage_bar.set_show_text(True)
        self.ram_usage_bar.get_style_context().add_class("resource-progressbar")
        self.ram_usage_bar.set_hexpand(True)
        res_grid.attach(ram_label, 0, 1, 1, 1)
        res_grid.attach(self.ram_usage_bar, 1, 1, 1, 1)

        self.statusbar = Gtk.Label(label="")
        self.statusbar.get_style_context().add_class("desc-label")
        self.left_box.pack_end(self.statusbar, False, False, 0)

    def build_right_panel(self):
        title = Gtk.Label(label="DIGITAL WELLBEING")
        title.get_style_context().add_class("category-header")
        self.right_box.pack_start(title, False, False, 0)

        self.total_usage_label = Gtk.Label(label="Total Usage: --:--:--")
        self.total_usage_label.get_style_context().add_class("desc-label")
        self.right_box.pack_start(self.total_usage_label, False, False, 0)

        sw = Gtk.ScrolledWindow()
        sw.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        sw.set_vexpand(True)
        sw.set_hexpand(True)
        sw.set_margin_top(10)

        self.wellbeing_container = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=15)
        sw.add(self.wellbeing_container)

        self.right_box.pack_start(sw, True, True, 0)

    def update_info(self):
        # Update Fan Info
        try:
            with open(FAN_CONTROL_FILE, 'r') as f: d = f.read()
            rpm = re.search(r"speed:\s*(\d+)", d)
            lvl = re.search(r"level:\s*([\w-]+)", d)
            self.fan_value_label.set_text(f"{rpm.group(1)} RPM" if rpm else "N/A")
            current = lvl.group(1) if lvl else None
        except Exception: self.fan_value_label.set_text("Error"); current = None
        for k, b in self.control_buttons.items():
            ctx = b.get_style_context(); ctx.remove_class("active")
            if k == current: ctx.add_class("active")

        # Update Temp Info
        try:
            output = subprocess.run(["sensors", "-j"], capture_output=True, text=True).stdout
            j = json.loads(output)
            core = next((v for k, v in j.items() if 'coretemp' in k), {})
            t = next((v['temp1_input'] for v in core.values() if isinstance(v, dict) and 'temp1_input' in v), None)
            self.temp_value_label.set_text(f"{t:.0f}°C" if t else "N/A")
        except Exception: self.temp_value_label.set_text("Error")

        # Update new sections
        self.update_resources()
        self.update_wellbeing_info()

        # Update status bar
        self.statusbar.set_text(f"Last update: {datetime.now().strftime('%H:%M:%S')}")
        return True

    def update_resources(self):
        # Update CPU Usage
        try:
            with open('/proc/stat', 'r') as f:
                fields = [float(column) for column in f.readline().strip().split()[1:]]

            idle, total = fields[3], sum(fields)
            idle_delta, total_delta = idle - self.last_cpu_info['idle'], total - self.last_cpu_info['total']
            self.last_cpu_info = {'idle': idle, 'total': total}

            usage = 1.0 - (idle_delta / total_delta) if total_delta > 0 else 0
            self.cpu_usage_bar.set_fraction(usage)
            self.cpu_usage_bar.set_text(f"{usage:.0%}")
        except Exception:
            self.cpu_usage_bar.set_text("Error")

        # Update RAM Usage
        try:
            output = subprocess.run(["free", "-m"], capture_output=True, text=True, check=True).stdout
            lines = output.splitlines()
            mem_line = next(line for line in lines if line.startswith("Mem:"))
            parts = mem_line.split()
            total_ram, used_ram = int(parts[1]), int(parts[2])

            fraction = used_ram / total_ram
            self.ram_usage_bar.set_fraction(fraction)
            self.ram_usage_bar.set_text(f"{used_ram} / {total_ram} MB")
        except (subprocess.SubprocessError, StopIteration, ValueError, IndexError):
            self.ram_usage_bar.set_text("Error")

    def time_str_to_seconds(self, time_str):
        seconds = 0
        try:
            h = re.search(r'(\d+)h', time_str); m = re.search(r'(\d+)m', time_str); s = re.search(r'(\d+)s', time_str)
            if h: seconds += int(h.group(1)) * 3600
            if m: seconds += int(m.group(1)) * 60
            if s: seconds += int(s.group(1))
        except (TypeError, ValueError): return 0
        return seconds

    def update_wellbeing_info(self):
        for child in self.wellbeing_container.get_children():
            child.destroy()
        try:
            with open(WELLBEING_DATA_FILE, 'r') as f:
                lines = [self.ansi_escape.sub('', line) for line in f.read().splitlines()]
            total_usage = next((line.split()[-1] for line in lines if "Today's Screen Usage" in line), None)
            self.total_usage_label.set_text(f"Total Usage: {total_usage}" if total_usage else "Total Usage: N/A")
            app_data = []
            in_data_section = False
            for line in lines:
                if in_data_section and line.strip() and not line.startswith('+--'):
                    parts = line.rsplit(None, 1)
                    if len(parts) == 2:
                        app_data.append({'name': parts[0].strip(), 'time_str': parts[1].strip()})
                if "App" in line and "Time" in line and '---' not in line:
                    in_data_section = True
            if not app_data: return
            for app in app_data: app['seconds'] = self.time_str_to_seconds(app['time_str'])
            app_data.sort(key=lambda x: x['seconds'], reverse=True)
            max_seconds = app_data[0]['seconds'] if app_data else 1
            for app in app_data:
                fraction = app['seconds'] / max_seconds if max_seconds > 0 else 0
                row_widget = self.create_wellbeing_row(app['name'], app['time_str'], fraction)
                self.wellbeing_container.pack_start(row_widget, False, False, 0)
            self.wellbeing_container.show_all()
        except Exception:
            self.total_usage_label.set_text("Wellbeing data error")

    def create_wellbeing_row(self, name, time_str, fraction):
        grid = Gtk.Grid(row_spacing=5)
        name_label = Gtk.Label(label=name, xalign=0)
        name_label.get_style_context().add_class("app-name-label")
        grid.attach(name_label, 0, 0, 1, 1)
        overlay = Gtk.Overlay()
        progress = Gtk.ProgressBar()
        progress.set_fraction(fraction)
        progress.get_style_context().add_class("wellbeing-progressbar")
        overlay.add(progress)
        time_label_on_bar = Gtk.Label(label=time_str)
        time_label_on_bar.get_style_context().add_class("progressbar-label")
        time_label_on_bar.set_halign(Gtk.Align.CENTER)
        time_label_on_bar.set_valign(Gtk.Align.CENTER)
        overlay.add_overlay(time_label_on_bar)
        grid.attach(overlay, 0, 1, 1, 1)
        return grid

    def on_set_mode(self, _, mode):
        try:
            with open(FAN_CONTROL_FILE, 'w') as f: f.write(f"level {mode}")
        except Exception as e: self.statusbar.set_text(f"Error setting fan: {e}")
        GLib.idle_add(self.update_info)

    def on_key_press(self, _, event):
        if event.keyval == Gdk.KEY_Escape: Gtk.main_quit()
        elif event.keyval == Gdk.KEY_c: self.toggle_right_panel()

    def toggle_right_panel(self):
        self.collapsed = not self.collapsed
        self.right_box.set_visible(not self.collapsed)

def load_css():
    provider = Gtk.CssProvider()
    script_dir = os.path.dirname(os.path.abspath(__file__))
    css_file_path = os.path.join(script_dir, "thinkfanui.css")
    try:
        provider.load_from_path(css_file_path)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(), provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
    except GLib.Error as e:
        print(f"Error loading CSS file: {e}")

if __name__ == "__main__":
    if os.geteuid() != 0:
        print("This script requires root privileges to control the fan. Please run with sudo.")
        exit(1)
    load_css()
    win = SigmaFanDashboard(); win.show_all()
    Gtk.main()
