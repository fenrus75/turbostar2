project('turbostar', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++23'])

cc = meson.get_compiler('cpp')
ncurses_dep = dependency('ncursesw', required : false)
if not ncurses_dep.found()
  ncurses_dep = cc.find_library('ncursesw', required : true)
endif


subdir('src')

executable('turbostar',
  turbostar_sources,
  dependencies : [ncurses_dep],
  install : true)

# --- Tests ---
# Unit tests
test_event_logger = executable('test_event_logger',
  ['tests/unit/test_event_logger.cpp', 'src/event_logger.cpp'],
  include_directories: 'src')
test('unit_event_logger', test_event_logger)

# E2E tests (run via python)
pymod = import('python')
python = pymod.find_installation('python3', required: false)

if python.found()
  test('e2e_basic', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_basic.py')],
    env: [
      'MESON_BUILD_ROOT=' + meson.project_build_root(),
      'PYTHONPATH=' + join_paths(meson.project_source_root(), 'tests/e2e')
    ],
    workdir: meson.project_build_root(),
    timeout: 10
  )
  test('e2e_menus', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_menus.py')],
    env: [
      'MESON_BUILD_ROOT=' + meson.project_build_root(),
      'PYTHONPATH=' + join_paths(meson.project_source_root(), 'tests/e2e')
    ],
    workdir: meson.project_build_root(),
    timeout: 60
  )
endif

