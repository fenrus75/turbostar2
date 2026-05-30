project('turbostar', ['cpp', 'c'],
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++23'])

add_project_arguments('-DTURBOSTAR_VERSION="' + meson.project_version() + '"', language : 'cpp')

cc = meson.get_compiler('cpp')
git_prog = find_program('git', required: false)
ncurses_dep = dependency('ncursesw', required : false)
if not ncurses_dep.found()
  ncurses_dep = cc.find_library('ncursesw', required : true)

endif

if not cc.has_header('dtl/dtl.hpp')
  error('dtl/dtl.hpp is required but not found. Please install libdtl-dev (or equivalent).')
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

# Provide a libunwind dependency.
cc = meson.get_compiler('c')
libunwind_dep = cc.find_library('unwind', required: true)

libturbocatch = shared_library('turbocatch',
  'src/crash_catcher/crash_catcher.c',
  dependencies: [libunwind_dep],
  install: false
)

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

test_ansi_terminal_emulator = executable('test_ansi_terminal_emulator',
  ['tests/unit/test_ansi_terminal_emulator.cpp', 'src/ui/ansi_terminal_emulator.cpp', 'src/event_logger.cpp', 'src/utf8.cpp'],
  include_directories: 'src')
test('unit_ansi_terminal_emulator', test_ansi_terminal_emulator)

test_history_manager = executable('test_history_manager',
  ['tests/unit/test_history_manager.cpp', 'src/history_manager.cpp', 'src/event_logger.cpp', 'src/fs_utils.cpp', 'src/git_manager.cpp', 'src/config_manager.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/command_runner.cpp', 'src/event_queue.cpp', 'src/crashdump_manager.cpp', 'src/project_manager.cpp', 'src/lsp_manager.cpp'],
  dependencies : [lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep],
  include_directories: 'src')
test('unit_history_manager', test_history_manager)

test_command_runner = executable('test_command_runner',
  ['tests/unit/test_command_runner.cpp', 'src/command_runner.cpp', 'src/config_manager.cpp', 'src/event_logger.cpp', 'src/crashdump_manager.cpp', 'src/fs_utils.cpp', 'src/git_manager.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/event_queue.cpp', 'src/project_manager.cpp', 'src/lsp_manager.cpp'],
  dependencies : [lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep],
  include_directories: 'src')
test('unit_command_runner', test_command_runner)

test_tool_infrastructure = executable('test_tool_infrastructure',
  ['tests/unit/test_tool_infrastructure.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp'],
  dependencies : [nlohmann_json_dep],
  include_directories: 'src')
test('unit_tool_infrastructure', test_tool_infrastructure)

test_tools = executable('test_tools',
  'tests/unit/test_tools.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_tools', test_tools)

test_shell_security = executable('test_shell_security',
  'tests/unit/test_shell_security.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_shell_security', test_shell_security)

test_activate_skill = executable('test_activate_skill',
  'tests/unit/test_activate_skill.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_activate_skill', test_activate_skill)

test_agent_add_todo = executable('test_agent_add_todo',
  'tests/unit/test_agent_add_todo.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_add_todo', test_agent_add_todo)

test_agent_complete_todo = executable('test_agent_complete_todo',
  'tests/unit/test_agent_complete_todo.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_complete_todo', test_agent_complete_todo)

test_agent_compress_history = executable('test_agent_compress_history',
  'tests/unit/test_agent_compress_history.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_compress_history', test_agent_compress_history)

test_agent_create = executable('test_agent_create',
  'tests/unit/test_agent_create.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_create', test_agent_create)

test_agent_delete_todo = executable('test_agent_delete_todo',
  'tests/unit/test_agent_delete_todo.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_delete_todo', test_agent_delete_todo)

test_agent_end = executable('test_agent_end',
  'tests/unit/test_agent_end.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_end', test_agent_end)

test_agent_get_output = executable('test_agent_get_output',
  'tests/unit/test_agent_get_output.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_get_output', test_agent_get_output)

test_agent_get_run_screenshot = executable('test_agent_get_run_screenshot',
  'tests/unit/test_agent_get_run_screenshot.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_get_run_screenshot', test_agent_get_run_screenshot)

test_agent_list = executable('test_agent_list',
  'tests/unit/test_agent_list.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_list', test_agent_list)

test_agent_list_todos = executable('test_agent_list_todos',
  'tests/unit/test_agent_list_todos.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_list_todos', test_agent_list_todos)

test_agent_mark_episode = executable('test_agent_mark_episode',
  'tests/unit/test_agent_mark_episode.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_mark_episode', test_agent_mark_episode)

test_agent_set_status = executable('test_agent_set_status',
  'tests/unit/test_agent_set_status.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_set_status', test_agent_set_status)

test_agent_start_app = executable('test_agent_start_app',
  'tests/unit/test_agent_start_app.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_start_app', test_agent_start_app)

test_agent_status = executable('test_agent_status',
  'tests/unit/test_agent_status.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_status', test_agent_status)

test_agent_terminate_run = executable('test_agent_terminate_run',
  'tests/unit/test_agent_terminate_run.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_terminate_run', test_agent_terminate_run)

test_agent_todo_status = executable('test_agent_todo_status',
  'tests/unit/test_agent_todo_status.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_todo_status', test_agent_todo_status)

test_agent_write_to_run = executable('test_agent_write_to_run',
  'tests/unit/test_agent_write_to_run.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_write_to_run', test_agent_write_to_run)

test_ask_user = executable('test_ask_user',
  'tests/unit/test_ask_user.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_ask_user', test_ask_user)

test_clear_all_errors = executable('test_clear_all_errors',
  'tests/unit/test_clear_all_errors.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_clear_all_errors', test_clear_all_errors)

test_code_get_definition = executable('test_code_get_definition',
  'tests/unit/test_code_get_definition.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_code_get_definition', test_code_get_definition)

test_code_get_references = executable('test_code_get_references',
  'tests/unit/test_code_get_references.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_code_get_references', test_code_get_references)

test_code_get_scope = executable('test_code_get_scope',
  'tests/unit/test_code_get_scope.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_code_get_scope', test_code_get_scope)

test_crashdump_get_info = executable('test_crashdump_get_info',
  'tests/unit/test_crashdump_get_info.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_crashdump_get_info', test_crashdump_get_info)

test_crashdump_list = executable('test_crashdump_list',
  'tests/unit/test_crashdump_list.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_crashdump_list', test_crashdump_list)

test_flag_as_error = executable('test_flag_as_error',
  'tests/unit/test_flag_as_error.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_flag_as_error', test_flag_as_error)

test_fs_list_dir = executable('test_fs_list_dir',
  'tests/unit/test_fs_list_dir.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_list_dir', test_fs_list_dir)

test_open_in_editor = executable('test_open_in_editor',
  'tests/unit/test_open_in_editor.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_open_in_editor', test_open_in_editor)

test_vim_emulation = executable('test_vim_emulation',
  'tests/unit/test_vim_emulation.cpp',
  dependencies : [ui_dep, agent_dep, tools_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_vim_emulation', test_vim_emulation)

test_file_security = executable('test_file_security',
  ['tests/unit/test_file_security.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp'],
  dependencies : [],
  include_directories: 'src')
test('unit_file_security', test_file_security)
test_fs_replace_lines = executable('test_fs_replace_lines',
  ['tests/unit/test_fs_replace_lines.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/agentlib/tool_registry.cpp', 'src/tools/fs_replace_lines/fs_replace_lines_entry.cpp', 'src/tools/fs_replace_lines/fs_replace_lines_security.cpp', 'src/agentlib/interactions/base.cpp', 'src/markdown_utils.cpp', 'src/utf8.cpp'],
  dependencies : [nlohmann_json_dep],
  include_directories: 'src')
test('unit_fs_replace_lines', test_fs_replace_lines)

test_virtual_file_system = executable('test_virtual_file_system',
  ['tests/unit/test_virtual_file_system.cpp', 'src/agentlib/virtual_file_system.cpp'],
  dependencies : [],
  include_directories: 'src')
test('unit_virtual_file_system', test_virtual_file_system)

test_skill_manager = executable('test_skill_manager',
  ['tests/unit/test_skill_manager.cpp', 'src/agentlib/skill_manager.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/event_logger.cpp', 'src/markdown_utils.cpp', 'src/utf8.cpp'],
  dependencies : [],
  include_directories: 'src')
test('unit_skill_manager', test_skill_manager)


test_python_highlighter = executable('test_python_highlighter',
  ['tests/unit/test_python_highlighter.cpp', 'src/python_highlighter.cpp', 'src/line.cpp', 'src/utf8.cpp'],
  dependencies : [re2_dep],
  include_directories: 'src')
test('unit_python_highlighter', test_python_highlighter)

test_markdown_highlighter = executable('test_markdown_highlighter',
  ['tests/unit/test_markdown_highlighter.cpp', 'src/markdown_highlighter.cpp', 'src/line.cpp', 'src/markdown_utils.cpp', 'src/utf8.cpp'],
  dependencies : [re2_dep],
  include_directories: 'src')
test('unit_markdown_highlighter', test_markdown_highlighter)



test_markdown_utils = executable('test_markdown_utils',
  ['tests/unit/test_markdown_utils.cpp', 'src/markdown_utils.cpp', 'src/utf8.cpp'],
  dependencies : [],
  include_directories: 'src')
test('unit_markdown_utils', test_markdown_utils)

test_utf8 = executable('test_utf8',
  ['tests/unit/test_utf8.cpp', 'src/utf8.cpp'],
  dependencies : [],
  include_directories: 'src')
test('unit_utf8', test_utf8)


test_document = executable('test_document',
  'tests/unit/test_document.cpp',
  dependencies : [core_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: inc)
test('unit_document', test_document, is_parallel: false)

test_gcc_log_parser = executable('test_gcc_log_parser',
  'tests/unit/test_gcc_log_parser.cpp',
  dependencies : [core_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: inc)
test('unit_gcc_log_parser', test_gcc_log_parser)

test_line = executable('test_line',
  'tests/unit/test_line.cpp',
  dependencies : [core_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: inc)
test('unit_line', test_line)




test_sqlite_validation = executable('test_sqlite_validation',
  ['tests/unit/test_sqlite_validation.cpp', 'src/fs_utils.cpp', 'src/git_manager.cpp', 'src/command_runner.cpp', 'src/config_manager.cpp', 'src/event_logger.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/agentlib/tool_registry.cpp', 'src/agentlib/skill_manager.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/agentlib/file_security_manager.cpp', 'src/event_queue.cpp', 'src/crashdump_manager.cpp', 'src/tools/sqlite_create_db/sqlite_create_db_entry.cpp', 'src/tools/sqlite_create_db/sqlite_create_db_security.cpp', 'src/tools/sqlite_delete_db/sqlite_delete_db_entry.cpp', 'src/tools/sqlite_delete_db/sqlite_delete_db_security.cpp', 'src/tools/sqlite_perform/sqlite_perform_entry.cpp', 'src/tools/sqlite_perform/sqlite_perform_security.cpp', 'src/project_manager.cpp', 'src/lsp_manager.cpp', 'src/markdown_utils.cpp', 'src/utf8.cpp'],
  dependencies : [nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: 'src')
test('unit_sqlite_validation', test_sqlite_validation)

test_crashdump_clear = executable('test_crashdump_clear',
  ['tests/unit/test_crashdump_clear.cpp', 'src/crashdump_manager.cpp', 'src/fs_utils.cpp', 'src/git_manager.cpp', 'src/command_runner.cpp', 'src/config_manager.cpp', 'src/event_logger.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/event_queue.cpp', 'src/tools/crashdump_clear/crashdump_clear_entry.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/agentlib/interactions/action.cpp', 'src/agentlib/interactions/base.cpp', 'src/markdown_utils.cpp', 'src/project_manager.cpp', 'src/lsp_manager.cpp', 'src/utf8.cpp'],
  dependencies : [lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep],
  include_directories: 'src')
test('unit_crashdump_clear', test_crashdump_clear)

test_pop_todo = executable('test_pop_todo',
  ['tests/unit/test_pop_todo.cpp', 'src/agentlib/ai_agent.cpp', 'src/agentlib/compaction_engine.cpp', 'src/agentlib/llm_client.cpp', 'src/agentlib/api_formatter.cpp', 'src/agentlib/httplib_transport.cpp', 'src/agentlib/ai_model.cpp', 'src/agentlib/tool_registry.cpp', 'src/agentlib/skill_manager.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/agentlib/interactions/action.cpp', 'src/agentlib/interactions/base.cpp', 'src/agentlib/interactions/reasoning.cpp', 'src/agentlib/interactions/llm_response.cpp', 'src/agentlib/interactions/user_message.cpp', 'src/agentlib/interactions/system_message.cpp', 'src/agentlib/interactions/terminal.cpp', 'src/agentlib/interactions/tool_interaction.cpp', 'src/markdown_utils.cpp', 'src/fs_utils.cpp', 'src/git_manager.cpp', 'src/event_logger.cpp', 'src/config_manager.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/command_runner.cpp', 'src/event_queue.cpp', 'src/crashdump_manager.cpp', 'src/lsp_manager.cpp', 'src/project_manager.cpp', 'src/utf8.cpp'],
  dependencies : [lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, httplib_dep, thread_dep],
  include_directories: 'src')
test('unit_pop_todo', test_pop_todo)

test_httplib_transport = executable('test_httplib_transport',
  ['tests/unit/test_httplib_transport.cpp', 'src/agentlib/httplib_transport.cpp'],
  dependencies : [lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, httplib_dep, thread_dep],
  include_directories: 'src')
test('unit_httplib_transport', test_httplib_transport)

test_fs_find_in_files = executable('test_fs_find_in_files',
  ['tests/unit/test_fs_find_in_files.cpp', 'src/tools/fs_find_in_files/fs_find_in_files_entry.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/agentlib/interactions/action.cpp', 'src/agentlib/interactions/base.cpp', 'src/agentlib/interactions/tool_interaction.cpp', 'src/markdown_utils.cpp', 'src/fs_utils.cpp', 'src/git_manager.cpp', 'src/event_logger.cpp', 'src/config_manager.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/command_runner.cpp', 'src/event_queue.cpp', 'src/crashdump_manager.cpp', 'src/lsp_manager.cpp', 'src/project_manager.cpp', 'src/utf8.cpp'],
  dependencies : [lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep],
  include_directories: 'src')
test('unit_fs_find_in_files', test_fs_find_in_files)

test_project_layout = executable('test_project_layout',
  ['tests/unit/test_project_layout.cpp', 'src/project_manager.cpp', 'src/git_manager.cpp', 'src/crashdump_manager.cpp', 'src/lsp_manager.cpp', 'src/event_logger.cpp', 'src/config_manager.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/command_runner.cpp', 'src/event_queue.cpp', 'src/fs_utils.cpp'],
  dependencies : [lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep],
  include_directories: 'src')
test('unit_project_layout', test_project_layout)

test_output_filter = executable('test_output_filter',
  ['tests/unit/test_output_filter.cpp'],
  include_directories: 'src')
test('unit_output_filter', test_output_filter)

test_run_config = executable('test_run_config',
  'tests/unit/test_run_config.cpp',
  dependencies : [ui_dep, agent_dep, tools_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_run_config', test_run_config)


# E2E tests (run via python)
uv = find_program('uv', required: false)
pymod = import('python')
python = pymod.find_installation('python3', required: false)

if uv.found()
  e2e_exe = uv
  e2e_base_args = ['run', '--with', 'pyte', '--with', 'pytest', 'python3']
elif python.found()
  e2e_exe = python
  e2e_base_args = []
else
  e2e_exe = disabler()
  e2e_base_args = []
endif

if uv.found() or python.found()
  e2e_env = [
    'MESON_BUILD_ROOT=' + meson.project_build_root(),
    'PYTHONPATH=' + join_paths(meson.project_source_root(), 'tests/e2e'),
    'PROJECT_ROOT=' + meson.project_source_root()
  ]
  
  test('e2e_context_paging', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_context_paging.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 180,
    is_parallel: false
  )
  test('e2e_exit_delay', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_exit_delay.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_basic', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_basic.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_readonly_help', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_readonly_help.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_menus', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_menus.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_cursor', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_cursor.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_cursor_wrap', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_cursor_wrap.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_typing', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_typing.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_utf8', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_utf8.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_backspace', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_backspace.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_enter', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_enter.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_delete_line', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_delete_line.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_compaction', e2e_exe,
    args: e2e_base_args + ['-m', 'pytest', join_paths(meson.project_source_root(), 'tests/e2e/test_compaction.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_selection', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_selection.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_power_nav', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_power_nav.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_undo_redo', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_undo_redo.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_insert_file', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_insert_file.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_format', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_format.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_settings', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_settings.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_bracket_matching', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_bracket_matching.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_scope_selection', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_scope_selection.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_multiple_filenames', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_multiple_filenames.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  if git_prog.found()
    test('e2e_git', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_git.py')],
      env: e2e_env,
      workdir: meson.project_build_root(),
      timeout: 60
    )
    test('e2e_git_mouse', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_git_mouse.py')],
      env: e2e_env,
      workdir: meson.project_build_root(),
      timeout: 60
    )
  
endif

  test('e2e_save_mouse', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_save_mouse.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_mouse_scroll_zorder', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_mouse_scroll_zorder.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_window_drag_resize', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_window_drag_resize.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_window_maximize', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_window_maximize.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_window_popup', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_window_popup.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_close_dirty', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_close_dirty.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_untitled_save_loop', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_untitled_save_loop.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_force_quit', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_force_quit.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_block_copy_move', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_block_copy_move.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_block_move_down', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_block_move_down.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_word_line_edit', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_word_line_edit.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_content_assertion', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_content_assertion.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_dialog_esc', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_dialog_esc.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_dialogs', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_dialogs.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_menu_dialogs', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_menu_dialogs.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_menu_windows', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_menu_windows.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_extra_keys', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_extra_keys.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_search', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_search.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_search_autocomplete', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_search_autocomplete.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_file_dialog', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_file_dialog.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_advanced_search', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_advanced_search.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_ghost_x', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_ghost_x.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_tabs', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_tabs.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_syntax', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_syntax.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_syntax_markdown', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_syntax_markdown.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_ctrl_sequences', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_ctrl_sequences.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_block_delete', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_block_delete.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_format_paragraph', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_format_paragraph.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_agent_todos', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_todos.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_agent_create', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_create.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )

  test('e2e_agent_status_window', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_status_window.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_agent_mouse_scroll', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_mouse_scroll.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_sqlite_tools', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_sqlite_tools.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )

  test('e2e_debugger', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_debugger.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )

  test('e2e_linked_windows', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_linked_windows.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )

  test('e2e_ncurses_stuck', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_ncurses_stuck.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )

  # IMPORTANT: Keep e2e_lsp_selection as the very last test in this file.
  # It has a tendency to make later tests fail randomly if it is not last.
  test('e2e_lsp_selection', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_lsp_selection.py')],
    env: e2e_env,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
endif
