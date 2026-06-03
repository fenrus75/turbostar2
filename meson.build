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
curl_dep = dependency('libcurl', required : true)
nlohmann_json_dep = dependency('nlohmann_json', required : true)
thread_dep = dependency('threads')
re2_dep = dependency('re2', required : true)
sqlite3_dep = dependency('sqlite3', required : true)

lsp_fw = subproject('lsp-framework')
lsp_dep = lsp_fw.get_variable('lsp_dep')

# Define dependencies early
inc = include_directories('include', 'src')
ncurses = ncurses_dep

# Provide a libunwind dependency.
cc_c = meson.get_compiler('c')
libunwind_dep = cc_c.find_library('unwind', required: true)
dl_dep = cc_c.find_library('dl', required: false)

subdir('src')

libturbocatch = shared_library('turbocatch',
  'src/crash_catcher/crash_catcher.c',
  dependencies: [libunwind_dep, dl_dep],
  override_options: ['b_sanitize=none'],
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
copy_to_testrun_tgt = custom_target('copy_to_testrun',
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
  dependencies : [nlohmann_json_dep, httplib_dep, thread_dep, curl_dep],
  include_directories: 'src')
test('unit_tool_infrastructure', test_tool_infrastructure)

test_tools = executable('test_tools',
  ['tests/unit/test_tools.cpp',
   'src/tools/fs_list_tests/fs_list_tests_entry.cpp',
   'src/tools/fs_list_tests/fs_list_tests_security.cpp',
   'src/tools/fs_run_tests/fs_run_tests_entry.cpp',
   'src/tools/fs_run_tests/fs_run_tests_security.cpp',
   'src/tools/agent_set_timer/agent_set_timer_entry.cpp',
   'src/tools/agent_set_timer/agent_set_timer_security.cpp',
   'src/tools/agent_list_episodes/agent_list_episodes_entry.cpp',
   'src/tools/agent_list_episodes/agent_list_episodes_security.cpp',
   'src/tools/agent_add_todo/agent_add_todo_entry.cpp',
   'src/tools/agent_add_todo/agent_add_todo_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_tools', test_tools)

test_shell_security = executable('test_shell_security',
  ['tests/unit/test_shell_security.cpp',
   'src/tools/git_add/git_add_entry.cpp',
   'src/tools/git_add/git_add_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_shell_security', test_shell_security)

test_activate_skill = executable('test_activate_skill',
  ['tests/unit/test_activate_skill.cpp',
   'src/tools/activate_skill/activate_skill_entry.cpp',
   'src/tools/activate_skill/activate_skill_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_activate_skill', test_activate_skill)

test_list_skills = executable('test_list_skills',
  ['tests/unit/test_list_skills.cpp',
   'src/tools/list_skills/list_skills_entry.cpp',
   'src/tools/list_skills/list_skills_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_list_skills', test_list_skills)

test_list_tool_calls = executable('test_list_tool_calls',
  ['tests/unit/test_list_tool_calls.cpp',
   'src/tools/list_tool_calls/list_tool_calls_entry.cpp',
   'src/tools/list_tool_calls/list_tool_calls_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_list_tool_calls', test_list_tool_calls)

test_message_agent = executable('test_message_agent',
  ['tests/unit/test_message_agent.cpp',
   'src/tools/message_agent/message_agent_entry.cpp',
   'src/tools/message_agent/message_agent_security.cpp',
   'src/tools/agent_create/agent_create_entry.cpp',
   'src/tools/agent_create/agent_create_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_message_agent', test_message_agent)

test_run_python = executable('test_run_python',
  ['tests/unit/test_run_python.cpp',
   'src/tools/run_python/run_python_entry.cpp',
   'src/tools/run_python/run_python_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_run_python', test_run_python)

test_run_shell_command = executable('test_run_shell_command',
  ['tests/unit/test_run_shell_command.cpp',
   'src/tools/run_shell_command/run_shell_command_entry.cpp',
   'src/tools/run_shell_command/run_shell_command_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_run_shell_command', test_run_shell_command)

test_sqlite_list_db = executable('test_sqlite_list_db',
  ['tests/unit/test_sqlite_list_db.cpp',
   'src/tools/sqlite_list_db/sqlite_list_db_entry.cpp',
   'src/tools/sqlite_list_db/sqlite_list_db_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_sqlite_list_db', test_sqlite_list_db)

test_git_branch_create = executable('test_git_branch_create',
  ['tests/unit/test_git_branch_create.cpp',
   'src/tools/git_branch_create/git_branch_create_entry.cpp',
   'src/tools/git_branch_create/git_branch_create_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_branch_create', test_git_branch_create, is_parallel: false)

test_git_branch_list = executable('test_git_branch_list',
  ['tests/unit/test_git_branch_list.cpp',
   'src/tools/git_branch_list/git_branch_list_entry.cpp',
   'src/tools/git_branch_list/git_branch_list_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_branch_list', test_git_branch_list, is_parallel: false)

test_git_checkout_branch = executable('test_git_checkout_branch',
  ['tests/unit/test_git_checkout_branch.cpp',
   'src/tools/git_checkout_branch/git_checkout_branch_entry.cpp',
   'src/tools/git_checkout_branch/git_checkout_branch_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_checkout_branch', test_git_checkout_branch, is_parallel: false)

test_git_commit = executable('test_git_commit',
  ['tests/unit/test_git_commit.cpp',
   'src/tools/git_commit/git_commit_entry.cpp',
   'src/tools/git_commit/git_commit_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_commit', test_git_commit, is_parallel: false)

test_git_diff_from_branch = executable('test_git_diff_from_branch',
  ['tests/unit/test_git_diff_from_branch.cpp',
   'src/tools/git_diff_from_branch/git_diff_from_branch_entry.cpp',
   'src/tools/git_diff_from_branch/git_diff_from_branch_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_diff_from_branch', test_git_diff_from_branch, is_parallel: false)

test_git_diff_staged = executable('test_git_diff_staged',
  ['tests/unit/test_git_diff_staged.cpp',
   'src/tools/git_diff_staged/git_diff_staged_entry.cpp',
   'src/tools/git_diff_staged/git_diff_staged_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_diff_staged', test_git_diff_staged, is_parallel: false)

test_git_diff_unstaged = executable('test_git_diff_unstaged',
  ['tests/unit/test_git_diff_unstaged.cpp',
   'src/tools/git_diff_unstaged/git_diff_unstaged_entry.cpp',
   'src/tools/git_diff_unstaged/git_diff_unstaged_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_diff_unstaged', test_git_diff_unstaged, is_parallel: false)

test_git_init = executable('test_git_init',
  ['tests/unit/test_git_init.cpp',
   'src/tools/git_init/git_init_entry.cpp',
   'src/tools/git_init/git_init_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_init', test_git_init, is_parallel: false)

test_git_log = executable('test_git_log',
  ['tests/unit/test_git_log.cpp',
   'src/tools/git_log/git_log_entry.cpp',
   'src/tools/git_log/git_log_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_log', test_git_log, is_parallel: false)

test_git_pull = executable('test_git_pull',
  ['tests/unit/test_git_pull.cpp',
   'src/tools/git_pull/git_pull_entry.cpp',
   'src/tools/git_pull/git_pull_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_pull', test_git_pull, is_parallel: false)

test_git_push = executable('test_git_push',
  ['tests/unit/test_git_push.cpp',
   'src/tools/git_push/git_push_entry.cpp',
   'src/tools/git_push/git_push_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_push', test_git_push, is_parallel: false)

test_git_restore = executable('test_git_restore',
  ['tests/unit/test_git_restore.cpp',
   'src/tools/git_restore/git_restore_entry.cpp',
   'src/tools/git_restore/git_restore_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_restore', test_git_restore, is_parallel: false)

test_git_status = executable('test_git_status',
  ['tests/unit/test_git_status.cpp',
   'src/tools/git_status/git_status_entry.cpp',
   'src/tools/git_status/git_status_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_status', test_git_status, is_parallel: false)

test_git_unstage = executable('test_git_unstage',
  ['tests/unit/test_git_unstage.cpp',
   'src/tools/git_unstage/git_unstage_entry.cpp',
   'src/tools/git_unstage/git_unstage_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_unstage', test_git_unstage, is_parallel: false)

test_wait_for_agent = executable('test_wait_for_agent',
  ['tests/unit/test_wait_for_agent.cpp',
   'src/tools/wait_for_agent/wait_for_agent_entry.cpp',
   'src/tools/wait_for_agent/wait_for_agent_security.cpp',
   'src/tools/agent_create/agent_create_entry.cpp',
   'src/tools/agent_create/agent_create_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_wait_for_agent', test_wait_for_agent)

test_web_fetch = executable('test_web_fetch',
  ['tests/unit/test_web_fetch.cpp',
   'src/tools/web_fetch/web_fetch_entry.cpp',
   'src/tools/web_fetch/web_fetch_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_web_fetch', test_web_fetch)


test_agent_add_todo = executable('test_agent_add_todo',
  ['tests/unit/test_agent_add_todo.cpp',
   'src/tools/agent_add_todo/agent_add_todo_entry.cpp',
   'src/tools/agent_add_todo/agent_add_todo_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_add_todo', test_agent_add_todo)

test_agent_complete_todo = executable('test_agent_complete_todo',
  ['tests/unit/test_agent_complete_todo.cpp',
   'src/tools/agent_complete_todo/agent_complete_todo_entry.cpp',
   'src/tools/agent_complete_todo/agent_complete_todo_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_complete_todo', test_agent_complete_todo)

test_agent_compress_history = executable('test_agent_compress_history',
  ['tests/unit/test_agent_compress_history.cpp',
   'src/tools/agent_compress_history/agent_compress_history_entry.cpp',
   'src/tools/agent_compress_history/agent_compress_history_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_compress_history', test_agent_compress_history)

test_exit_summarization = executable('test_exit_summarization',
  ['tests/unit/test_exit_summarization.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_exit_summarization', test_exit_summarization)

test_agent_create = executable('test_agent_create',
  ['tests/unit/test_agent_create.cpp',
   'src/tools/agent_create/agent_create_entry.cpp',
   'src/tools/agent_create/agent_create_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_create', test_agent_create)

test_agent_delete_todo = executable('test_agent_delete_todo',
  ['tests/unit/test_agent_delete_todo.cpp',
   'src/tools/agent_delete_todo/agent_delete_todo_entry.cpp',
   'src/tools/agent_delete_todo/agent_delete_todo_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_delete_todo', test_agent_delete_todo)

test_agent_end = executable('test_agent_end',
  ['tests/unit/test_agent_end.cpp',
   'src/tools/agent_end/agent_end_entry.cpp',
   'src/tools/agent_end/agent_end_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_end', test_agent_end)

test_agent_get_output = executable('test_agent_get_output',
  ['tests/unit/test_agent_get_output.cpp',
   'src/tools/agent_get_output/agent_get_output_entry.cpp',
   'src/tools/agent_get_output/agent_get_output_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_get_output', test_agent_get_output)

test_agent_get_run_screenshot = executable('test_agent_get_run_screenshot',
  ['tests/unit/test_agent_get_run_screenshot.cpp',
   'src/tools/agent_get_run_screenshot/agent_get_run_screenshot_entry.cpp',
   'src/tools/agent_get_run_screenshot/agent_get_run_screenshot_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_get_run_screenshot', test_agent_get_run_screenshot)

test_agent_list = executable('test_agent_list',
  ['tests/unit/test_agent_list.cpp',
   'src/tools/agent_list/agent_list_entry.cpp',
   'src/tools/agent_list/agent_list_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_list', test_agent_list)

test_agent_list_todos = executable('test_agent_list_todos',
  ['tests/unit/test_agent_list_todos.cpp',
   'src/tools/agent_list_todos/agent_list_todos_entry.cpp',
   'src/tools/agent_list_todos/agent_list_todos_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_list_todos', test_agent_list_todos)

test_agent_mark_episode = executable('test_agent_mark_episode',
  ['tests/unit/test_agent_mark_episode.cpp',
   'src/tools/agent_mark_episode/agent_mark_episode_entry.cpp',
   'src/tools/agent_mark_episode/agent_mark_episode_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_mark_episode', test_agent_mark_episode)

test_agent_set_status = executable('test_agent_set_status',
  ['tests/unit/test_agent_set_status.cpp',
   'src/tools/agent_set_status/agent_set_status_entry.cpp',
   'src/tools/agent_set_status/agent_set_status_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_set_status', test_agent_set_status)

test_agent_start_app = executable('test_agent_start_app',
  ['tests/unit/test_agent_start_app.cpp',
   'src/tools/agent_start_app/agent_start_app_entry.cpp',
   'src/tools/agent_start_app/agent_start_app_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_start_app', test_agent_start_app)

test_agent_status = executable('test_agent_status',
  ['tests/unit/test_agent_status.cpp',
   'src/tools/agent_status/agent_status_entry.cpp',
   'src/tools/agent_status/agent_status_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_status', test_agent_status)

test_agent_terminate_run = executable('test_agent_terminate_run',
  ['tests/unit/test_agent_terminate_run.cpp',
   'src/tools/agent_terminate_run/agent_terminate_run_entry.cpp',
   'src/tools/agent_terminate_run/agent_terminate_run_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_terminate_run', test_agent_terminate_run)

test_agent_todo_status = executable('test_agent_todo_status',
  ['tests/unit/test_agent_todo_status.cpp',
   'src/tools/agent_todo_status/agent_todo_status_entry.cpp',
   'src/tools/agent_todo_status/agent_todo_status_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_todo_status', test_agent_todo_status)

test_agent_write_to_run = executable('test_agent_write_to_run',
  ['tests/unit/test_agent_write_to_run.cpp',
   'src/tools/agent_write_to_run/agent_write_to_run_entry.cpp',
   'src/tools/agent_write_to_run/agent_write_to_run_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_write_to_run', test_agent_write_to_run)

test_ask_user = executable('test_ask_user',
  ['tests/unit/test_ask_user.cpp',
   'src/tools/ask_user/ask_user_entry.cpp',
   'src/tools/ask_user/ask_user_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_ask_user', test_ask_user)

test_clear_all_errors = executable('test_clear_all_errors',
  ['tests/unit/test_clear_all_errors.cpp',
   'src/tools/clear_all_errors/clear_all_errors_entry.cpp',
   'src/tools/clear_all_errors/clear_all_errors_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_clear_all_errors', test_clear_all_errors)

test_code_get_definition = executable('test_code_get_definition',
  ['tests/unit/test_code_get_definition.cpp',
   'src/tools/code_get_definition/code_get_definition_entry.cpp',
   'src/tools/code_get_definition/code_get_definition_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_code_get_definition', test_code_get_definition, is_parallel: false)

test_code_get_references = executable('test_code_get_references',
  ['tests/unit/test_code_get_references.cpp',
   'src/tools/code_get_references/code_get_references_entry.cpp',
   'src/tools/code_get_references/code_get_references_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_code_get_references', test_code_get_references, is_parallel: false)

test_code_get_scope = executable('test_code_get_scope',
  ['tests/unit/test_code_get_scope.cpp',
   'src/tools/code_get_scope/code_get_scope_entry.cpp',
   'src/tools/code_get_scope/code_get_scope_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_code_get_scope', test_code_get_scope, is_parallel: false)

test_crashdump_get_info = executable('test_crashdump_get_info',
  ['tests/unit/test_crashdump_get_info.cpp',
   'src/tools/crashdump_get_info/crashdump_get_info_entry.cpp',
   'src/tools/crashdump_get_info/crashdump_get_info_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_crashdump_get_info', test_crashdump_get_info)

test_assert_fail = executable('test_assert_fail',
  'tests/unit/test_assert_fail.cpp',
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_assert_fail', test_assert_fail, is_parallel: false)

test_fallback_crash = executable('test_fallback_crash',
  'tests/unit/test_fallback_crash.cpp',
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fallback_crash', test_fallback_crash, is_parallel: false)

test_crashdump_list = executable('test_crashdump_list',
  ['tests/unit/test_crashdump_list.cpp',
   'src/tools/crashdump_list/crashdump_list_entry.cpp',
   'src/tools/crashdump_list/crashdump_list_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_crashdump_list', test_crashdump_list)

test_flag_as_error = executable('test_flag_as_error',
  ['tests/unit/test_flag_as_error.cpp',
   'src/tools/flag_as_error/flag_as_error_entry.cpp',
   'src/tools/flag_as_error/flag_as_error_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_flag_as_error', test_flag_as_error)

test_fs_list_dir = executable('test_fs_list_dir',
  ['tests/unit/test_fs_list_dir.cpp',
   'src/tools/fs_list_dir/fs_list_dir_entry.cpp',
   'src/tools/fs_list_dir/fs_list_dir_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_list_dir', test_fs_list_dir)

test_fs_mkdir = executable('test_fs_mkdir',
  ['tests/unit/test_fs_mkdir.cpp',
   'src/tools/fs_mkdir/fs_mkdir_entry.cpp',
   'src/tools/fs_mkdir/fs_mkdir_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_mkdir', test_fs_mkdir)

test_fs_read_binary = executable('test_fs_read_binary',
  ['tests/unit/test_fs_read_binary.cpp',
   'src/tools/fs_read_binary/fs_read_binary_entry.cpp',
   'src/tools/fs_read_binary/fs_read_binary_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_read_binary', test_fs_read_binary)

test_fs_regexp_lines = executable('test_fs_regexp_lines',
  ['tests/unit/test_fs_regexp_lines.cpp',
   'src/tools/fs_regexp_lines/fs_regexp_lines_entry.cpp',
   'src/tools/fs_regexp_lines/fs_regexp_lines_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_regexp_lines', test_fs_regexp_lines)

test_fs_compile_file = executable('test_fs_compile_file',
  ['tests/unit/test_fs_compile_file.cpp',
   'src/tools/fs_compile_file/fs_compile_file_entry.cpp',
   'src/tools/fs_compile_file/fs_compile_file_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_compile_file', test_fs_compile_file)

test_fs_compile_info = executable('test_fs_compile_info',
  ['tests/unit/test_fs_compile_info.cpp',
   'src/tools/fs_compile_info/fs_compile_info_entry.cpp',
   'src/tools/fs_compile_info/fs_compile_info_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_compile_info', test_fs_compile_info)

test_fs_compile_project = executable('test_fs_compile_project',
  ['tests/unit/test_fs_compile_project.cpp',
   'src/tools/fs_compile_project/fs_compile_project_entry.cpp',
   'src/tools/fs_compile_project/fs_compile_project_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_compile_project', test_fs_compile_project)

test_fs_compile_summary = executable('test_fs_compile_summary',
  ['tests/unit/test_fs_compile_summary.cpp',
   'src/tools/fs_compile_summary/fs_compile_summary_entry.cpp',
   'src/tools/fs_compile_summary/fs_compile_summary_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_compile_summary', test_fs_compile_summary)

test_get_current_datetime = executable('test_get_current_datetime',
  ['tests/unit/test_get_current_datetime.cpp',
   'src/tools/get_current_datetime/get_current_datetime_entry.cpp',
   'src/tools/get_current_datetime/get_current_datetime_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_get_current_datetime', test_get_current_datetime)


test_open_in_editor = executable('test_open_in_editor',
  ['tests/unit/test_open_in_editor.cpp',
   'src/tools/open_in_editor/open_in_editor_entry.cpp',
   'src/tools/open_in_editor/open_in_editor_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_open_in_editor', test_open_in_editor)

test_vim_emulation = executable('test_vim_emulation',
  'tests/unit/test_vim_emulation.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_vim_emulation', test_vim_emulation)

test_file_security = executable('test_file_security',
  ['tests/unit/test_file_security.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp'],
  dependencies : [httplib_dep, thread_dep, nlohmann_json_dep, curl_dep],
  include_directories: 'src')
test('unit_file_security', test_file_security)
test_fs_replace_lines = executable('test_fs_replace_lines',
  ['tests/unit/test_fs_replace_lines.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/agentlib/tool_registry.cpp', 'src/tools/fs_replace_lines/fs_replace_lines_entry.cpp', 'src/tools/fs_replace_lines/fs_replace_lines_security.cpp', 'src/agentlib/interactions/base.cpp', 'src/markdown_utils.cpp', 'src/utf8.cpp'],
  dependencies : [nlohmann_json_dep, httplib_dep, thread_dep, curl_dep],
  include_directories: 'src')
test('unit_fs_replace_lines', test_fs_replace_lines)

test_virtual_file_system = executable('test_virtual_file_system',
  ['tests/unit/test_virtual_file_system.cpp', 'src/agentlib/virtual_file_system.cpp'],
  dependencies : [httplib_dep, thread_dep, nlohmann_json_dep, curl_dep],
  include_directories: 'src')
test('unit_virtual_file_system', test_virtual_file_system)

test_github_vfs = executable('test_github_vfs',
  ['tests/unit/test_github_vfs.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/agentlib/file_security_manager.cpp'],
  dependencies : [lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, httplib_dep, thread_dep, curl_dep],
  include_directories: 'src')
test('unit_github_vfs', test_github_vfs)

test_skill_manager = executable('test_skill_manager',
  ['tests/unit/test_skill_manager.cpp', 'src/agentlib/skill_manager.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/event_logger.cpp', 'src/markdown_utils.cpp', 'src/utf8.cpp'],
  dependencies : [httplib_dep, thread_dep, nlohmann_json_dep, curl_dep],
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

test_context_dnn = executable('test_context_dnn',
  'tests/unit/test_context_dnn.cpp',
  dependencies : [agent_dep, core_dep, nlohmann_json_dep, thread_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: inc)
test('unit_context_dnn', test_context_dnn)

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
  dependencies : [nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep, httplib_dep, thread_dep, curl_dep],
  include_directories: 'src')
test('unit_sqlite_validation', test_sqlite_validation)

test_crashdump_clear = executable('test_crashdump_clear',
  ['tests/unit/test_crashdump_clear.cpp', 'src/crashdump_manager.cpp', 'src/fs_utils.cpp', 'src/git_manager.cpp', 'src/command_runner.cpp', 'src/config_manager.cpp', 'src/event_logger.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/event_queue.cpp', 'src/tools/crashdump_clear/crashdump_clear_entry.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/agentlib/interactions/action.cpp', 'src/agentlib/interactions/base.cpp', 'src/markdown_utils.cpp', 'src/project_manager.cpp', 'src/lsp_manager.cpp', 'src/utf8.cpp'],
  dependencies : [lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, httplib_dep, thread_dep, curl_dep],
  include_directories: 'src')
test('unit_crashdump_clear', test_crashdump_clear)

test_pop_todo = executable('test_pop_todo',
  ['tests/unit/test_pop_todo.cpp', 'src/agentlib/ai_agent.cpp', 'src/agentlib/compaction_engine.cpp', 'src/agentlib/context_dnn.cpp', 'src/agentlib/llm_client.cpp', 'src/agentlib/api_formatter.cpp', 'src/agentlib/httplib_transport.cpp', 'src/agentlib/ai_model.cpp', 'src/agentlib/tool_registry.cpp', 'src/agentlib/skill_manager.cpp', 'src/agentlib/file_security_manager.cpp', 'src/agentlib/virtual_file_system.cpp', 'src/agentlib/interactions/action.cpp', 'src/agentlib/interactions/base.cpp', 'src/agentlib/interactions/reasoning.cpp', 'src/agentlib/interactions/llm_response.cpp', 'src/agentlib/interactions/user_message.cpp', 'src/agentlib/interactions/system_message.cpp', 'src/agentlib/interactions/terminal.cpp', 'src/agentlib/interactions/tool_interaction.cpp', 'src/markdown_utils.cpp', 'src/fs_utils.cpp', 'src/git_manager.cpp', 'src/event_logger.cpp', 'src/config_manager.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/command_runner.cpp', 'src/event_queue.cpp', 'src/crashdump_manager.cpp', 'src/lsp_manager.cpp', 'src/project_manager.cpp', 'src/utf8.cpp'],
  dependencies : [lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, httplib_dep, thread_dep, curl_dep],
  include_directories: 'src')
test('unit_pop_todo', test_pop_todo)

test_httplib_transport = executable('test_httplib_transport',
  ['tests/unit/test_httplib_transport.cpp', 'src/agentlib/httplib_transport.cpp'],
  dependencies : [lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, httplib_dep, thread_dep],
  include_directories: 'src')
test('unit_httplib_transport', test_httplib_transport)

test_fs_grep_files = executable('test_fs_grep_files',
  ['tests/unit/test_fs_grep_files.cpp', 'src/tools/fs_grep_files/fs_grep_files_entry.cpp', 'src/tools/fs_grep_files/fs_grep_files_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_fs_grep_files', test_fs_grep_files)

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
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_run_config', test_run_config)

test_tool_status = executable('test_tool_status',
  'tests/unit/test_tool_status.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_tool_status', test_tool_status)


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
    'PROJECT_ROOT=' + meson.project_source_root(),
    'UV_OFFLINE=1'
  ]
  e2e_depends = [turbostar_exe, agentcli_record_exe, agentcli_replay_exe, copy_to_testrun_tgt]

  test('e2e_context_paging', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_context_paging.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 180,
    is_parallel: false
  )
  test('e2e_exit_delay', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_exit_delay.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_basic', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_basic.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_mouse_copy', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_mouse_copy.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_disk_change', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_disk_change.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_readonly_help', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_readonly_help.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_menus', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_menus.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_cursor', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_cursor.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_cursor_wrap', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_cursor_wrap.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_typing', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_typing.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_utf8', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_utf8.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_backspace', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_backspace.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_enter', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_enter.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_delete_line', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_delete_line.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_compaction', e2e_exe,
    args: e2e_base_args + ['-m', 'pytest', join_paths(meson.project_source_root(), 'tests/e2e/test_compaction.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_selection', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_selection.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_power_nav', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_power_nav.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_undo_redo', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_undo_redo.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_insert_file', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_insert_file.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_format', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_format.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_settings', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_settings.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_bracket_matching', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_bracket_matching.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_scope_selection', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_scope_selection.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_multiple_filenames', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_multiple_filenames.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  if git_prog.found()
    test('e2e_git', e2e_exe,
      args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_git.py')],
      env: e2e_env,
    depends: e2e_depends,
      workdir: meson.project_build_root(),
      timeout: 60,
      is_parallel: false
    )
    test('e2e_git_mouse', e2e_exe,
      args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_git_mouse.py')],
      env: e2e_env,
    depends: e2e_depends,
      workdir: meson.project_build_root(),
      timeout: 60,
      is_parallel: false
    )
  endif

  test('e2e_save_mouse', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_save_mouse.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_mouse_scroll_zorder', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_mouse_scroll_zorder.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_window_drag_resize', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_window_drag_resize.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_window_maximize', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_window_maximize.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_window_popup', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_window_popup.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_close_dirty', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_close_dirty.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_untitled_save_loop', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_untitled_save_loop.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_force_quit', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_force_quit.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_block_copy_move', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_block_copy_move.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_block_move_down', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_block_move_down.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )

  test('e2e_word_line_edit', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_word_line_edit.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_content_assertion', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_content_assertion.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_dialog_esc', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_dialog_esc.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_dialogs', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_dialogs.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_menu_dialogs', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_menu_dialogs.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_menu_windows', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_menu_windows.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_extra_keys', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_extra_keys.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_search', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_search.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_search_autocomplete', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_search_autocomplete.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_file_dialog', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_file_dialog.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_advanced_search', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_advanced_search.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_ghost_x', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_ghost_x.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_tabs', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_tabs.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_syntax', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_syntax.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_syntax_markdown', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_syntax_markdown.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_ctrl_sequences', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_ctrl_sequences.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_block_delete', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_block_delete.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60
  )
  test('e2e_format_paragraph', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_format_paragraph.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_agent_todos', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_todos.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_agent_create', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_create.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )

  test('e2e_agent_status_window', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_status_window.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_agent_mouse_scroll', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_mouse_scroll.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_agent_mouse_copy', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_mouse_copy.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
  test('e2e_sqlite_tools', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_sqlite_tools.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )

  test('e2e_debugger', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_debugger.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )

  test('e2e_linked_windows', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_linked_windows.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )

  test('e2e_ncurses_stuck', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_ncurses_stuck.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )

  # IMPORTANT: Keep e2e_lsp_selection as the very last test in this file.
  # It has a tendency to make later tests fail randomly if it is not last.
  test('e2e_lsp_selection', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_lsp_selection.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false
  )
endif
