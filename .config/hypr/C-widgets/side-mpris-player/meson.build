project('mpris-lyrics-viewer', 'c',
  version : '1.0.0',
  default_options : ['warning_level=2', 'c_std=c11'])

# Find dependencies
gtk_dep = dependency('gtk4')
layershell_dep = dependency('gtk4-layer-shell-0')
json_glib_dep = dependency('json-glib-1.0')
libsoup_dep = dependency('libsoup-3.0')

# Find the C math library (libm) as a dependency
cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required : true)


# List all your source files
sources = [
  'src/main.c',
  'src/utils.c',
  'src/mpris.c',
  'src/lyrics.c'
]

# Define the executable
executable('mpris-lyrics-viewer', sources,
  c_args : ['-pthread', '-D_GNU_SOURCE'],
  dependencies : [gtk_dep, layershell_dep, json_glib_dep, math_dep, libsoup_dep],
  install : true)