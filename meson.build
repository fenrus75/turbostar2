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
  e2e_env = [
    'MESON_BUILD_ROOT=' + meson.project_build_root(),
    'PYTHONPATH=' + join_paths(meson.project_source_root(), 'tests/e2e'),
    'PROJECT_ROOT=' + meson.project_source_root()
  ]
  
  test('e2e_basic', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_basic.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 10
  )
  test('e2e_menus', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_menus.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_cursor', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_cursor.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 10
  )
  test('e2e_typing', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_typing.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 10
  )
  test('e2e_utf8', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_utf8.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 10
  )
  test('e2e_backspace', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_backspace.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 10
  )
  test('e2e_enter', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_enter.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 10
  )
  test('e2e_delete_line', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_delete_line.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 10
  )
  test('e2e_selection', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_selection.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_power_nav', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_power_nav.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_block_copy_move', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_block_copy_move.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_word_line_edit', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_word_line_edit.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_content_assertion', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_content_assertion.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_dialogs', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_dialogs.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_menu_dialogs', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_menu_dialogs.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_extra_keys', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_extra_keys.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_syntax', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_syntax.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_ctrl_sequences', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_ctrl_sequences.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_block_delete', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_block_delete.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
endif
