tests = executable(
  'aurorae-tests',
  'test-main.cpp',
  'test-config.cpp',
  'test-frame.cpp',
  'test-layout.cpp',
  dependencies: aurorae_core_dep,
  cpp_args: ['-DTEST_DATA_DIR="' + meson.current_source_dir() / 'data' + '"'],
)

test('core', tests)
