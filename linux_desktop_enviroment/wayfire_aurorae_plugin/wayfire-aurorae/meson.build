project(
  'wayfire-aurorae',
  'cpp',
  version: '0.1.0',
  meson_version: '>=0.64.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
    'buildtype=debugoptimized',
    'b_ndebug=if-release',
  ],
)

cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')
librsvg = dependency('librsvg-2.0', version: '>=2.52')
libxml = dependency('libxml-2.0')
gio = dependency('gio-2.0')

# Renderer-agnostic core: theme parsing, FrameSvg layout, rasterization,
# decoration geometry. No Wayfire dependency, so the preview tool and the
# tests use exactly the code the plugin runs.
core_sources = files(
  'src/core/app-icons.cpp',
  'src/core/buttons.cpp',
  'src/core/cairo-backend.cpp',
  'src/core/caption.cpp',
  'src/core/decoration-layout.cpp',
  'src/core/decoration-painter.cpp',
  'src/core/draw-list.cpp',
  'src/core/frame-svg.cpp',
  'src/core/ini-file.cpp',
  'src/core/kde-settings.cpp',
  'src/core/raster.cpp',
  'src/core/settings.cpp',
  'src/core/svg-document.cpp',
  'src/core/theme-config.cpp',
  'src/core/theme.cpp',
)

core_deps = [cairo, pango, pangocairo, librsvg, libxml, gio]
core_inc = include_directories('src')

aurorae_core = static_library(
  'aurorae-core',
  core_sources,
  include_directories: core_inc,
  dependencies: core_deps,
  pic: true,
)

aurorae_core_dep = declare_dependency(
  link_with: aurorae_core,
  include_directories: core_inc,
  dependencies: core_deps,
)

executable(
  'aurorae-preview',
  'tools/aurorae-preview.cpp',
  dependencies: aurorae_core_dep,
  install: get_option('install_preview'),
)

wayfire = dependency('wayfire', required: get_option('plugin'))
if wayfire.found()
  wlroots = dependency('wlroots-0.19')
  wfconfig = dependency('wf-config')

  shared_module(
    'aurorae',
    files(
      'src/wayfire/decoration-node.cpp',
      'src/wayfire/decorator.cpp',
      'src/wayfire/gpu-texture.cpp',
      'src/wayfire/plugin.cpp',
      'src/wayfire/window-actions.cpp',
    ),
    dependencies: [aurorae_core_dep, wayfire, wlroots, wfconfig],
    # Wayfire's headers expect these (see wayfire's own plugin builds).
    cpp_args: ['-DWLR_USE_UNSTABLE', '-DWAYFIRE_PLUGIN'],
    install: true,
    install_dir: wayfire.get_variable(pkgconfig: 'plugindir'),
  )

  install_data(
    'metadata/aurorae.xml',
    install_dir: wayfire.get_variable(pkgconfig: 'metadatadir'),
  )
endif

if get_option('tests')
  subdir('tests')
endif
