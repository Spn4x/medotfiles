project('hypr-sidebar', 'c',
  version : '0.1.0',
  default_options : ['warning_level=2', 'c_std=c11'])

# Find dependencies
gtk_dep = dependency('gtk4')
adw_dep = dependency('libadwaita-1')
layershell_dep = dependency('gtk4-layer-shell-0')
json_glib_dep = dependency('json-glib-1.0')
pipewire_dep = dependency('libpipewire-0.3')
fftw_dep = dependency('fftw3')

# --- FINAL FIX: Find the C math library (libm) as a dependency ---
cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required : true)


# List all your source files
sources = [
  'src/main.c',
  'src/app_dock.c',
  'src/control_center.c',
  'src/utils.c',
  'src/popout_panels.c'
]

# Define the executable
executable('hypr-sidebar', sources,

  c_args : ['-pthread', '-D_GNU_SOURCE'],
  
  dependencies : [gtk_dep, adw_dep, layershell_dep, json_glib_dep, pipewire_dep, fftw_dep, math_dep],
  install : true)