project('control-center', 'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    # 'b_sanitize=address' has been removed
  ]
)

sources = [
  'src/main.c',
  'src/utils.c',
  'src/network_manager.c',
  'src/bluetooth_manager.c',
  'src/wifi_scanner.c',
  'src/bluetooth_scanner.c',
  'src/audio_manager.c',
  'src/brightness_manager.c',
  'src/system_monitor.c',

]

dependencies = [
  dependency('gtk4'),
  dependency('libadwaita-1'),
  dependency('gtk4-layer-shell-0'),
]

executable('control-center', sources,
  dependencies: dependencies,
  install: true,
)