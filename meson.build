project('turbostar', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++23'])

add_project_arguments('-DTURBOSTAR_VERSION="' + meson.project_version() + '"', language : 'cpp')

cc = meson.get_compiler('cpp')
git_prog = find_program('git', required: false)
ncurses_dep = dependency('ncursesw', required : false)
if not ncurses_dep.found()
  ncurses_dep = cc.find_library('ncursesw', required : true)

endif

httplib_dep = dependency('cpp-httplib', required : true)
nlohmann_json_dep = dependency('nlohmann_json', required : true)
thread_dep = dependency('threads')
re2_dep = dependency('re2', required : true)
sqlite3_dep = dependency('sqlite3', required : true)

lsp_fw = subproject('lsp-framework')
lsp_dep = lsp_fw.get_variable('lsp_dep')

# Define dependencies early
inc = include_directories('include', 'src')
ncurses = ncurses_dep

subdir('src')

turbostar_exe = executable('turbostar',
  'src/main.cpp',
  dependencies : [ui_dep, agent_dep, tools_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc,
  install : true)

agentcli_exe = executable('agentcli',
  'src/agentcli/main.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc,
  install : false)

agentcli_record_exe = executable('agentcli_record',
  'src/agentcli/main.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc,
  cpp_args: ['-DLLM_TRANSPORT_RECORD'],
  install : false)

agentcli_replay_exe = executable('agentcli_replay',
  'src/agentcli/main.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc,
  cpp_args: ['-DLLM_TRANSPORT_REPLAY'],
  install : false)


# --- Automated binary deployment to testrun/ ---
# This ensures that E2E tests always use the latest built binary.
custom_target('copy_to_testrun',
  input: turbostar_exe,
  output: 'turbostar_deploy_dummy',
  command: ['sh', '-c', 'mkdir -p "$1" && cp "$2" "$1/turbostar"', 'sh', join_paths(meson.project_source_root(), 'testrun'), '@INPUT@'],
  build_by_default: true
)

# --- Tests ---
# Unit tests
test_event_logger = executable('test_event_logger',
  ['tests/unit/test_event_logger.cpp', 'src/event_logger.cpp'],
  include_directories: 'src')
test('unit_event_logger', test_event_logger)

test_history_manager = executable('test_history_manager',
  ['tests/unit/test_history_manager.cpp', 'src/history_manager.cpp', 'src/event_logger.cpp'],
  include_directories: 'src')
test('unit_history_manager', test_history_manager)

test_command_runner = executable('test_command_runner',
  ['tests/unit/test_command_runner.cpp', 'src/command_runner.cpp', 'src/config_manager.cpp', 'src/event_logger.cpp', 'src/coredump_manager.cpp'],
  include_directories: 'src')
test('unit_command_runner', test_command_runner)

test_tool_infrastructure = executable('test_tool_infrastructure',
  ['tests/unit/test_tool_infrastructure.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp'],
  dependencies : [nlohmann_json_dep],
  include_directories: 'src')
test('unit_tool_infrastructure', test_tool_infrastructure)

test_file_security = executable('test_file_security',
  ['tests/unit/test_file_security.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp'],
  dependencies : [],
  include_directories: 'src')
test('unit_file_security', test_file_security)

test_fs_replace_lines = executable('test_fs_replace_lines',
  ['tests/unit/test_fs_replace_lines.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/agentlib/tool_registry.cpp', 'src/tools/fs_replace_lines/fs_replace_lines_entry.cpp', 'src/tools/fs_replace_lines/fs_replace_lines_security.cpp'],
  dependencies : [nlohmann_json_dep],
  include_directories: 'src')
test('unit_fs_replace_lines', test_fs_replace_lines)

test_virtual_file_system = executable('test_virtual_file_system',
  ['tests/unit/test_virtual_file_system.cpp', 'src/agentlib/virtual_file_system.cpp'],
  dependencies : [],
  include_directories: 'src')
test('unit_virtual_file_system', test_virtual_file_system)

test_python_highlighter = executable('test_python_highlighter',
  ['tests/unit/test_python_highlighter.cpp', 'src/python_highlighter.cpp', 'src/line.cpp'],
  dependencies : [re2_dep],
  include_directories: 'src')
test('unit_python_highlighter', test_python_highlighter)

test_markdown_utils = executable('test_markdown_utils',
  ['tests/unit/test_markdown_utils.cpp', 'src/markdown_utils.cpp'],
  dependencies : [],
  include_directories: 'src')
test('unit_markdown_utils', test_markdown_utils)

test_sqlite_validation = executable('test_sqlite_validation',
  ['tests/unit/test_sqlite_validation.cpp', 'src/fs_utils.cpp', 'src/git_manager.cpp', 'src/command_runner.cpp', 'src/config_manager.cpp', 'src/event_logger.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/agentlib/tool_registry.cpp', 'src/agentlib/skill_manager.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/agentlib/file_security_manager.cpp', 'src/event_queue.cpp', 'src/coredump_manager.cpp', 'src/tools/sqlite_create_db/sqlite_create_db_entry.cpp', 'src/tools/sqlite_create_db/sqlite_create_db_security.cpp', 'src/tools/sqlite_delete_db/sqlite_delete_db_entry.cpp', 'src/tools/sqlite_delete_db/sqlite_delete_db_security.cpp', 'src/tools/sqlite_perform/sqlite_perform_entry.cpp', 'src/tools/sqlite_perform/sqlite_perform_security.cpp'],
  dependencies : [nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: 'src')
test('unit_sqlite_validation', test_sqlite_validation)

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
    timeout: 60
  )
  test('e2e_readonly_help', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_readonly_help.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
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
    timeout: 60
  )
  test('e2e_cursor_wrap', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_cursor_wrap.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_typing', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_typing.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_utf8', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_utf8.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_backspace', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_backspace.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_enter', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_enter.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_delete_line', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_delete_line.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
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
  test('e2e_undo_redo', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_undo_redo.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_insert_file', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_insert_file.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_format', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_format.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_settings', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_settings.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_bracket_matching', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_bracket_matching.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_scope_selection', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_scope_selection.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_multiple_filenames', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_multiple_filenames.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  if git_prog.found()
    test('e2e_git', python,
      args: [join_paths(meson.project_source_root(), 'tests/e2e/test_git.py')],
      env: e2e_env,
      workdir: meson.project_build_root(),
      timeout: 60
    )
    test('e2e_git_mouse', python,
      args: [join_paths(meson.project_source_root(), 'tests/e2e/test_git_mouse.py')],
      env: e2e_env,
      workdir: meson.project_build_root(),
      timeout: 60
    )
  
endif

  test('e2e_save_mouse', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_save_mouse.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_window_popup', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_window_popup.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_close_dirty', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_close_dirty.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_untitled_save_loop', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_untitled_save_loop.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_force_quit', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_force_quit.py')],
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

  test('e2e_block_move_down', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_block_move_down.py')],
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
  test('e2e_dialog_esc', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_dialog_esc.py')],
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
  test('e2e_menu_windows', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_menu_windows.py')],
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
  test('e2e_search', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_search.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_search_autocomplete', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_search_autocomplete.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_file_dialog', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_file_dialog.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_advanced_search', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_advanced_search.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_ghost_x', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_ghost_x.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_tabs', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_tabs.py')],
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
  test('e2e_syntax_markdown', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_syntax_markdown.py')],
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
  test('e2e_format_paragraph', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_format_paragraph.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_agent_todos', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_todos.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_agent_create', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_create.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_sqlite_tools', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_sqlite_tools.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )

  # IMPORTANT: Keep e2e_lsp_selection as the very last test in this file.
  # It has a tendency to make later tests fail randomly if it is not last.
  test('e2e_lsp_selection', python,
    args: [join_paths(meson.project_source_root(), 'tests/e2e/test_lsp_selection.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
endif
