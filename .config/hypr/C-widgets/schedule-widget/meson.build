project('gtk-schedule-app', 'c',
  version: '0.1.0',
  default_options: ['c_std=c11', 'warning_level=2']
)

# --- Dependencies ---
gtk_dep = dependency('gtk4', version: '>=4.8')
json_dep = dependency('json-glib-1.0')
# This is the key dependency for the desktop widget functionality
layershell_dep = dependency('gtk4-layer-shell-0')

# Compile resource files (like CSS) into the binary
gnome = import('gnome')
resources = gnome.compile_resources('resources',
  'data/com.meismeric.GtkSchedule.gresource.xml',
  source_dir: 'data'
)

# --- Executable ---
executable('gtk-schedule',
  'src/main.c',
  resources, # Link the compiled resources
  dependencies: [
    gtk_dep,
    json_dep,
    layershell_dep,
  ],
  install: true,
)