project('sleek-calendar', 'c',
  version: '0.1.0',
  default_options: ['c_std=c11', 'warning_level=2'])

# Find dependencies
gtk_dep = dependency('gtk4')
layer_shell_dep = dependency('gtk4-layer-shell-0')
json_glib_dep = dependency('json-glib-1.0')
# --- START OF FIX ---
# Find the math library ('-lm')
m_dep = meson.get_compiler('c').find_library('m', required: false)
# --- END OF FIX ---


# Define our source files
sources = [
  'src/main.c'
]

# Define the executable
executable('sleek-calendar', sources,
  # Add m_dep to the list of dependencies
  dependencies: [gtk_dep, layer_shell_dep, json_glib_dep, m_dep],
  install: true)