project('turbostar', ['cpp', 'c'],
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++23'])

add_project_arguments('-DTURBOSTAR_VERSION="' + meson.project_version() + '"', language : 'cpp')
add_project_arguments('-DTURBOSTAR_CRASHPROCESS_PATH="' + get_option('prefix') + '/' + get_option('bindir') + '/turbostar-crashprocess"', language : 'cpp')
add_project_arguments('-DCPPHTTPLIB_OPENSSL_SUPPORT', language : 'cpp')

cc = meson.get_compiler('cpp')

if cc.get_id() == 'clang'
  add_project_arguments('-Wthread-safety', language : 'cpp')
endif

if get_option('enable-analyzer')
  if cc.get_id() == 'gcc' and cc.has_argument('-fanalyzer')
    add_project_arguments('-fanalyzer', language : 'cpp')
  endif
endif
git_prog = find_program('git', required: false)
git_version_h = vcs_tag(
  input : 'src/git_version.h.in',
  output : 'git_version.h',
  fallback : meson.project_version(),
  command : ['git', 'describe', '--always', '--dirty']
)
ncurses_dep = dependency('ncursesw', required : false)
if not ncurses_dep.found()
  ncurses_dep = cc.find_library('ncursesw', required : true)

endif

if not cc.has_header('dtl/dtl.hpp')
  error('dtl/dtl.hpp is required but not found. Please install libdtl-dev (or equivalent).')
endif

embed_prog = find_program('python3')
research_agent_h = custom_target('research_agent.h',
  input : 'agents/research.md',
  output : 'research_agent.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'research_agent_md']
)

self_agent_h = custom_target('self_agent.h',
  input : 'agents/self.md',
  output : 'self_agent.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'self_agent_md']
)

system_docs_files = files(
  'system/languages/c17.md',
  'system/languages/cpp23.md',
  'system/languages/cpp23_short.md',
  'system/languages/default.md',
  'system/languages/default_short.md',
  'system/languages/python311.md',
  'system/languages/rust2021.md',
  'system/languages/typescript.md',
  'system/languages/verilog.md',
  'system/workflows/code_review.md',
  'system/workflows/crash_analysis.md',
)

system_docs_embedded_h = custom_target('system_docs_embedded.h',
  output : 'system_docs_embedded.h',
  depend_files : system_docs_files,
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_system_docs.py'), join_paths(meson.project_source_root(), 'system'), '@OUTPUT@']
)

httplib_dep = dependency('cpp-httplib', required : true)
curl_dep = dependency('libcurl', required : true)
openssl_dep = dependency('openssl', required : true)
nlohmann_json_dep = dependency('nlohmann_json', required : true)
thread_dep = dependency('threads')
re2_dep = dependency('re2', required : true)
sqlite3_dep = dependency('sqlite3', required : true)
zlib_dep = dependency('zlib', required : true)
liblzma_dep = dependency('liblzma', required : false)
if liblzma_dep.found()
  add_project_arguments('-DHAS_LIBLZMA', language : 'cpp')
endif
libzstd_dep = dependency('libzstd', required : false)
if libzstd_dep.found()
  add_project_arguments('-DHAS_LIBZSTD', language : 'cpp')
endif
bzip2_dep = dependency('bzip2', required : false)
if bzip2_dep.found()
  add_project_arguments('-DHAS_BZIP2', language : 'cpp')
endif
liblz4_dep = dependency('liblz4', required : false)
if liblz4_dep.found()
  add_project_arguments('-DHAS_LIBLZ4', language : 'cpp')
endif
yaml_cpp_dep = dependency('yaml-cpp', required : true)
enable_libmagic = get_option('enable-libmagic')
magic_dep = dependency('', required : false)
if not enable_libmagic.disabled()
  magic_lib = cc.find_library('magic', required : enable_libmagic.enabled())
  if magic_lib.found()
    magic_dep = declare_dependency(dependencies : [magic_lib], compile_args : ['-DHAS_LIBMAGIC'])
  endif
endif

lexbor_dep = dependency('lexbor', required : false)
if lexbor_dep.found()
  add_project_arguments('-DHAS_LEXBOR', language : 'cpp')
endif

graphicsmagick_dep = dependency('GraphicsMagick++', required : false)
if graphicsmagick_dep.found()
  add_project_arguments('-DHAS_GRAPHICSMAGICK', language : 'cpp')
endif


lsp_fw = subproject('lsp-framework')
lsp_dep = lsp_fw.get_variable('lsp_dep')

# Define dependencies early
inc = include_directories('include', 'src')
ncurses = ncurses_dep

# Provide a libunwind dependency.
cc_c = meson.get_compiler('c')
libunwind_dep = cc_c.find_library('unwind', required: true)
add_project_link_arguments('-lunwind', language : ['cpp', 'c'])
dl_dep = cc_c.find_library('dl', required: true)

zydis_found = false
zydis_dep = dependency('zydis', required: false)
if zydis_dep.found()
  zydis_found = true
else
  zydis_lib = cc.find_library('Zydis', required: false)
  if zydis_lib.found() and cc.has_header('Zydis/Zydis.h')
    zydis_dep = declare_dependency(dependencies : zydis_lib)
    zydis_found = true
  else
    zydis_dep = declare_dependency()
  endif
endif

if zydis_found
  add_project_arguments('-DHAVE_ZYDIS=1', language : 'cpp')
endif

libdir = get_option('libdir')
prefix = get_option('prefix')

# Ensure we have the full, absolute path
if libdir.startswith('/')
  plugin_dir = libdir / 'turbostar' / 'plugins'
else
  plugin_dir = prefix / libdir / 'turbostar' / 'plugins'
endif

libexecdir = get_option('libexecdir')
if libexecdir.startswith('/')
  turbocatch_dir = libexecdir / 'turbostar'
else
  turbocatch_dir = prefix / libexecdir / 'turbostar'
endif

libturbocatch = shared_library('turbocatch',
  ['src/crash_catcher/crash_catcher.c', 'src/crash_catcher/perf_catcher.c'],
  dependencies: [libunwind_dep, dl_dep],
  override_options: ['b_sanitize=none'],
  install: true,
  install_dir: turbocatch_dir
)

subdir('src')

turbostar_exe = executable('turbostar',
  ['src/main.cpp', 'src/crash_handler.cpp', git_version_h],
  dependencies : [ui_dep, agent_dep, tools_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true,
  install : true)

turbostar_crashprocess_exe = executable('turbostar-crashprocess',
  ['src/crash_process.cpp', 'src/address_lookup.cpp'],
  include_directories: inc,
  install : true)

install_symlink('turboagent',
  install_dir: 'bin',
  pointing_to: 'turbostar'
)

install_symlink('turboserver',
  install_dir: 'bin',
  pointing_to: 'turbostar'
)

agentcli_exe = executable('agentcli',
  'src/agentcli/main.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true,
  install : false)

agentcli_record_exe = executable('agentcli_record',
  'src/agentcli/main.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  cpp_args: ['-DLLM_TRANSPORT_RECORD'],
  export_dynamic: true,
  install : false)

agentcli_replay_exe = executable('agentcli_replay',
  'src/agentcli/main.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  cpp_args: ['-DLLM_TRANSPORT_REPLAY'],
  export_dynamic: true,
  install : false)


# --- Automated binary deployment to testrun/ ---
# This ensures that E2E tests always use the latest built binary.
copy_to_testrun_tgt = custom_target('copy_to_testrun',
  input: turbostar_exe,
  output: 'turbostar_deploy_dummy',
  command: ['sh', '-c', 'mkdir -p "$1" && cp "$2" "$1/turbostar.new" && mv "$1/turbostar.new" "$1/turbostar" && ln -sf turbostar "$1/turboagent" && ln -sf turbostar "$1/turboserver"', 'sh', join_paths(meson.project_source_root(), 'testrun'), '@INPUT@'],
  build_by_default: true
)

add_test_setup('default',
  env: {'TURBOSTAR_IN_TESTSUITE': '1'},
  is_default: true
)

# --- Tests ---
# Unit tests
test_event_logger = executable('test_event_logger',
  ['tests/unit/test_event_logger.cpp', 'src/event_logger.cpp'],
  include_directories: 'src')
test('unit_event_logger', test_event_logger, suite: 'agent')

test_file_health_tracker = executable('test_file_health_tracker',
  ['tests/unit/test_file_health_tracker.cpp', 'src/event_logger.cpp', 'src/lsp_manager.cpp', 'src/fs_utils.cpp', 'src/git_manager.cpp', 'src/config_manager.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/command_runner.cpp', 'src/event_queue.cpp', 'src/crashdump_manager.cpp', 'src/address_lookup.cpp', 'src/project_manager.cpp', 'src/codereview_manager.cpp', 'src/perf_manager.cpp'],
  dependencies: [agent_dep, core_dep, lsp_dep, nlohmann_json_dep, thread_dep],
  include_directories: inc)
test('unit_file_health_tracker', test_file_health_tracker, suite: 'agent')

test_project_template_manager = executable('test_project_template_manager',
  ['tests/unit/test_project_template_manager.cpp'],
  dependencies : [core_dep],
  include_directories: inc)
test('unit_project_template_manager', test_project_template_manager, suite: 'agent')

test_ansi_terminal_emulator = executable('test_ansi_terminal_emulator',
  ['tests/unit/test_ansi_terminal_emulator.cpp', 'src/ui/ansi_terminal_emulator.cpp', 'src/event_logger.cpp', 'src/utf8.cpp', 'src/mime.cpp'],
  include_directories: 'src')
test('unit_ansi_terminal_emulator', test_ansi_terminal_emulator, suite: 'editor')

test_conversation_turns = executable('test_conversation_turns',
  ['tests/unit/test_conversation_turns.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_conversation_turns', test_conversation_turns, suite: 'agent')

test_pluginloader = executable('test_pluginloader',
  ['tests/unit/test_pluginloader.cpp'],
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true)
test('unit_pluginloader', test_pluginloader, suite: 'plugins', env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])

test_hexedit_plugin = executable('test_hexedit_plugin',
  ['tests/unit/test_hexedit_plugin.cpp',
   'src/plugins/hexedit/hexdump_tool.cpp',
   'src/plugins/hexedit/hexwrite_tool.cpp'],
  dependencies : [core_dep, agent_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: inc)
test('unit_hexedit_plugin', test_hexedit_plugin, suite: 'plugins')

test_a2a_validate_card = executable('test_a2a_validate_card',
  ['tests/unit/test_a2a_validate_card.cpp'],
  dependencies: [ui_dep, agent_dep, core_dep, ncurses_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true
)
test('unit_a2a_validate_card', test_a2a_validate_card, suite: 'plugins', env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])

test_a2a_generate_card_with_agent = executable('test_a2a_generate_card_with_agent',
  ['tests/unit/test_a2a_generate_card_with_agent.cpp'],
  dependencies: [ui_dep, agent_dep, core_dep, ncurses_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true
)
test('unit_a2a_generate_card_with_agent', test_a2a_generate_card_with_agent, suite: 'plugins', env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])

test_a2a_server = executable('test_a2a_server',
  ['tests/unit/test_a2a_server.cpp'],
  dependencies: [ui_dep, agent_dep, core_dep, ncurses_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true
)
test('unit_a2a_server', test_a2a_server, suite: 'plugins', env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])

test_a2a_client = executable('test_a2a_client',
  ['tests/unit/test_a2a_client.cpp'],
  dependencies: [ui_dep, agent_dep, core_dep, ncurses_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true
)
test('unit_a2a_client', test_a2a_client, suite: 'plugins', env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])

test_a2a_connect_server = executable('test_a2a_connect_server',
  ['tests/unit/test_a2a_connect_server.cpp'],
  dependencies: [ui_dep, agent_dep, core_dep, tools_dep, ncurses_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true
)
test('unit_a2a_connect_server', test_a2a_connect_server, suite: 'plugins', env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])

test_a2a_servers_dialog = executable('test_a2a_servers_dialog',
  ['tests/unit/test_a2a_servers_dialog.cpp'],
  dependencies: [ui_dep, agent_dep, core_dep, tools_dep, ncurses_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true
)
test('unit_a2a_servers_dialog', test_a2a_servers_dialog, suite: 'plugins', env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])

test_binary_plugin_impl = executable('test_binary_plugin_impl',
  ['tests/unit/test_binary_plugin.cpp',
   'src/plugins/binary/data_compress_tool.cpp',
   'src/plugins/binary/data_decompress_tool.cpp',
   'src/plugins/binary/data_compress_validator.cpp',
   'src/plugins/binary/data_decompress_validator.cpp',
   'src/plugins/binary/binary_utils.cpp'],
  dependencies : [core_dep, agent_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep, zlib_dep, liblzma_dep, libzstd_dep, bzip2_dep, liblz4_dep],
  include_directories: inc)
test('unit_binary_plugin', test_binary_plugin_impl, suite: 'plugins')


test_history_manager = executable('test_history_manager',
  ['tests/unit/test_history_manager.cpp', 'src/history_manager.cpp', 'src/event_logger.cpp', 'src/fs_utils.cpp', 'src/git_manager.cpp', 'src/config_manager.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/command_runner.cpp', 'src/event_queue.cpp', 'src/crashdump_manager.cpp', 'src/codemap_utils.cpp', 'src/address_lookup.cpp', 'src/project_manager.cpp', 'src/lsp_manager.cpp', 'src/codereview_manager.cpp', 'src/perf_manager.cpp'],
  dependencies : [agent_dep, core_dep, lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep],
  include_directories: 'src')
test('unit_history_manager', test_history_manager, suite: 'editor')

test_command_runner = executable('test_command_runner',
  ['tests/unit/test_command_runner.cpp', 'src/command_runner.cpp', 'src/config_manager.cpp', 'src/event_logger.cpp', 'src/crashdump_manager.cpp', 'src/codemap_utils.cpp', 'src/address_lookup.cpp', 'src/fs_utils.cpp', 'src/git_manager.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/event_queue.cpp', 'src/project_manager.cpp', 'src/lsp_manager.cpp', 'src/codereview_manager.cpp', 'src/perf_manager.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: 'src')
test('unit_command_runner', test_command_runner, suite: 'editor')

test_statistics_manager = executable('test_statistics_manager',
  ['tests/unit/test_statistics_manager.cpp', 'src/statistics_manager.cpp'],
  dependencies : [nlohmann_json_dep, thread_dep],
  include_directories: 'src')
test('unit_statistics_manager', test_statistics_manager, suite: 'editor')

test_tool_infrastructure = executable('test_tool_infrastructure',
  ['tests/unit/test_tool_infrastructure.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep],
  include_directories: inc)
test('unit_test_tool_infrastructure', test_tool_infrastructure, suite: 'agent')

test_clear_command = executable('test_clear_command',
  ['tests/unit/test_clear_command.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep],
  include_directories: inc)
test('unit_clear_command', test_clear_command, suite: 'agent')

test_address_lookup = executable('test_address_lookup',
  ['tests/unit/test_address_lookup.cpp', 'src/address_lookup.cpp'],
  dependencies : [nlohmann_json_dep, thread_dep],
  include_directories: 'src')
test('unit_address_lookup', test_address_lookup, suite: 'editor')

test_perf_catcher = executable('test_perf_catcher',
  ['tests/unit/test_perf_catcher.cpp'],
  dependencies : [thread_dep],
  include_directories: 'src')
test('unit_perf_catcher', test_perf_catcher, suite: 'editor', depends: libturbocatch)

test_perf_manager = executable('test_perf_manager',
  ['tests/unit/test_perf_manager.cpp', 'src/perf_manager.cpp', 'src/address_lookup.cpp'],
  dependencies : [core_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc)
test('unit_perf_manager', test_perf_manager, suite: 'editor', depends: libturbocatch)

test_profile_tools = executable('test_profile_tools',
  ['tests/unit/test_profile_tools.cpp',
   'src/tools/agent_get_profile_summary/agent_get_profile_summary_entry.cpp',
   'src/tools/agent_get_profile_summary/agent_get_profile_summary_security.cpp',
   'src/tools/agent_get_profile_details/agent_get_profile_details_entry.cpp',
   'src/tools/agent_get_profile_details/agent_get_profile_details_security.cpp',
   'src/perf_manager.cpp',
   'src/address_lookup.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc)
test('unit_profile_tools', test_profile_tools, suite: 'agent')

test_agent_highlight = executable('test_agent_highlight',
  ['tests/unit/test_agent_highlight.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc)
test('unit_test_agent_highlight', test_agent_highlight, suite: 'agent')

test_tools = executable('test_tools',
  ['tests/unit/test_tools.cpp',
   'src/tools/fs_run_tests/fs_run_tests_entry.cpp',
   'src/tools/fs_run_tests/fs_run_tests_security.cpp',
   'src/tools/agent_set_timer/agent_set_timer_entry.cpp',
   'src/tools/agent_set_timer/agent_set_timer_security.cpp',
   'src/tools/agent_list_episodes/agent_list_episodes_entry.cpp',
   'src/tools/agent_list_episodes/agent_list_episodes_security.cpp',
   'src/tools/apply_text_filter/apply_text_filter_entry.cpp',
   'src/tools/apply_text_filter/apply_text_filter_security.cpp',
   'src/codemap_utils.cpp',
   'src/tools/fs_read_lines/fs_read_lines_entry.cpp',
   'src/tools/fs_read_lines/fs_read_lines_security.cpp',
   'src/tools/fs_read_symbol/fs_read_symbol_entry.cpp',
   'src/tools/fs_read_symbol/fs_read_symbol_security.cpp',
   'src/tools/fs_write_file/fs_write_file_entry.cpp',
   'src/tools/fs_write_file/fs_write_file_security.cpp',
   'src/tools/fs_list_dir/fs_list_dir_entry.cpp',
   'src/tools/fs_list_dir/fs_list_dir_security.cpp',
   'src/tools/fs_purge_tmp/fs_purge_tmp_entry.cpp',
   'src/tools/fs_purge_tmp/fs_purge_tmp_security.cpp',
   'src/tools/fs_mkdir/fs_mkdir_entry.cpp',
   'src/tools/fs_mkdir/fs_mkdir_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_tools', test_tools, suite: 'agent')

test_shell_security = executable('test_shell_security',
  ['tests/unit/test_shell_security.cpp',
   'src/tools/git_add/git_add_entry.cpp',
   'src/tools/git_add/git_add_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_shell_security', test_shell_security, suite: 'agent')

test_fs_utils = executable('test_fs_utils',
  ['tests/unit/test_fs_utils.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_fs_utils', test_fs_utils, suite: 'agent')

test_fs_man = executable('test_fs_man',
  ['tests/unit/test_fs_man.cpp'],
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, zlib_dep],
  include_directories: inc)
test('unit_test_fs_man', test_fs_man, suite: 'agent')


test_activate_skill = executable('test_activate_skill',
  ['tests/unit/test_activate_skill.cpp',
   'src/tools/activate_skill/activate_skill_entry.cpp',
   'src/tools/activate_skill/activate_skill_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_activate_skill', test_activate_skill, suite: 'agent')

test_activate_tool_family = executable('test_activate_tool_family',
  ['tests/unit/test_activate_tool_family.cpp',
   'src/tools/activate_tool_family/activate_tool_family_entry.cpp',
   'src/tools/activate_tool_family/activate_tool_family_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_activate_tool_family', test_activate_tool_family, suite: 'agent')

test_x86_disassemble = executable('test_x86_disassemble',
  ['tests/unit/test_x86_disassemble.cpp',
   'src/plugins/asm86/x86_disassemble/x86_disassemble_entry.cpp',
   'src/plugins/asm86/x86_disassemble/x86_disassemble_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_x86_disassemble', test_x86_disassemble, suite: 'plugins')

test_x86_assemble = executable('test_x86_assemble',
  ['tests/unit/test_x86_assemble.cpp',
   'src/plugins/asm86/x86_assemble/x86_assemble_entry.cpp',
   'src/plugins/asm86/x86_assemble/x86_assemble_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_x86_assemble', test_x86_assemble, suite: 'plugins')


test_hexinspect = executable('test_hexinspect',
  ['tests/unit/test_hexinspect.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep, magic_dep],
  include_directories: inc,
  export_dynamic: true)
test('unit_test_hexinspect', test_hexinspect, suite: 'plugins', depends: hexedit_plugin, env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])

if lexbor_dep.found()
  test_html_extract_tables = executable('test_html_extract_tables',
    ['tests/unit/test_html_extract_tables.cpp'],
    dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
    include_directories: inc,
    export_dynamic: true)

  test('unit_test_html_extract_tables', test_html_extract_tables, suite: 'plugins', depends: html_plugin, env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])
endif

test_elf_list_sections = executable('test_elf_list_sections',
  ['tests/unit/test_elf_list_sections.cpp',
   'src/plugins/asm86/elf_list_sections/elf_list_sections_entry.cpp',
   'src/plugins/asm86/elf_list_sections/elf_list_sections_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_elf_list_sections', test_elf_list_sections, suite: 'plugins')

test_elf_list_symbols = executable('test_elf_list_symbols',
  ['tests/unit/test_elf_list_symbols.cpp',
   'src/plugins/asm86/elf_list_symbols/elf_list_symbols_entry.cpp',
   'src/plugins/asm86/elf_list_symbols/elf_list_symbols_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_elf_list_symbols', test_elf_list_symbols, suite: 'plugins')

test_security_scan_python = executable('test_security_scan_python',
  ['tests/unit/test_security_scan_python.cpp',
   'src/plugins/securityagent/security_scan_python/security_scan_python_entry.cpp',
   'src/plugins/securityagent/security_scan_python/security_scan_python_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_security_scan_python', test_security_scan_python, suite: 'plugins')

test_security_scan_c = executable('test_security_scan_c',
  ['tests/unit/test_security_scan_c.cpp',
   'src/plugins/securityagent/security_scan_c/security_scan_c_entry.cpp',
   'src/plugins/securityagent/security_scan_c/security_scan_c_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_security_scan_c', test_security_scan_c, suite: 'plugins')

test_security_scan_semgrep = executable('test_security_scan_semgrep',
  ['tests/unit/test_security_scan_semgrep.cpp',
   'src/plugins/securityagent/security_scan_semgrep/security_scan_semgrep_entry.cpp',
   'src/plugins/securityagent/security_scan_semgrep/security_scan_semgrep_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_security_scan_semgrep', test_security_scan_semgrep, suite: 'plugins', timeout: 90)

test_security_verify_html = executable('test_security_verify_html',
  ['tests/unit/test_security_verify_html.cpp',
   'src/plugins/securityagent/security_verify_html/security_verify_html_entry.cpp',
   'src/plugins/securityagent/security_verify_html/security_verify_html_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_security_verify_html', test_security_verify_html, suite: 'plugins')

test_send_message = executable('test_send_message',
  ['tests/unit/test_send_message.cpp',
   'src/tools/send_message/send_message_entry.cpp',
   'src/tools/send_message/send_message_security.cpp',
   'src/tools/invoke_subagent/invoke_subagent_entry.cpp',
   'src/tools/invoke_subagent/invoke_subagent_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_send_message', test_send_message, suite: 'agent')

test_run_python = executable('test_run_python',
  ['tests/unit/test_run_python.cpp',
   'src/tools/run_python/run_python_entry.cpp',
   'src/tools/run_python/run_python_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_run_python', test_run_python, timeout: 60, is_parallel: false, suite: 'agent')

test_run_shell_command = executable('test_run_shell_command',
  ['tests/unit/test_run_shell_command.cpp',
   'src/tools/run_shell_command/run_shell_command_entry.cpp',
   'src/tools/run_shell_command/run_shell_command_security.cpp',
   'src/tools/run_shell_command/shell_command_resteer.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_run_shell_command', test_run_shell_command, suite: 'agent')

test_shell_command_resteer = executable('test_shell_command_resteer',
  ['tests/unit/test_shell_command_resteer.cpp',
   'src/tools/run_shell_command/run_shell_command_entry.cpp',
   'src/tools/run_shell_command/run_shell_command_security.cpp',
   'src/tools/run_shell_command/shell_command_resteer.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_shell_command_resteer', test_shell_command_resteer, suite: 'agent')

test_markdown_extract_plugin = executable('test_markdown_extract_plugin',
  ['tests/unit/test_markdown_extract_plugin.cpp',
   'src/plugins/markdown_extract/markdown_extract_tool.cpp',
   'src/plugins/markdown_extract/markdown_extract_validator.cpp',
   'src/tools/report_final_result/report_final_result_entry.cpp',
   'src/tools/report_final_result/report_final_result_security.cpp',
   markdown_extract_agent_h],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_markdown_extract_plugin', test_markdown_extract_plugin, suite: 'plugins',
  depends: [markdown_extract_plugin])

test_sqlite_list_db = executable('test_sqlite_list_db',
  'tests/unit/test_sqlite_list_db.cpp',
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true)
test('unit_test_sqlite_list_db', test_sqlite_list_db, suite: 'agent',
  depends: [sqlite_plugin],
  env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])

test_git_branch_create = executable('test_git_branch_create',
  ['tests/unit/test_git_branch_create.cpp',
   'src/tools/git_branch_create/git_branch_create_entry.cpp',
   'src/tools/git_branch_create/git_branch_create_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_branch_create', test_git_branch_create, suite: 'agent')

test_git_branch_list = executable('test_git_branch_list',
  ['tests/unit/test_git_branch_list.cpp',
   'src/tools/git_branch_list/git_branch_list_entry.cpp',
   'src/tools/git_branch_list/git_branch_list_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_branch_list', test_git_branch_list, suite: 'agent')

test_git_manager = executable('test_git_manager',
  ['tests/unit/test_git_manager.cpp', 'src/git_manager.cpp', 'src/event_logger.cpp', 'src/fs_utils.cpp', 'src/command_runner.cpp', 'src/config_manager.cpp', 'src/build_error_manager.cpp', 'src/gcc_log_parser.cpp', 'src/event_queue.cpp', 'src/crashdump_manager.cpp', 'src/codemap_utils.cpp', 'src/address_lookup.cpp', 'src/project_manager.cpp', 'src/lsp_manager.cpp', 'src/codereview_manager.cpp', 'src/perf_manager.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_manager', test_git_manager, suite: 'agent')

test_git_checkout_branch = executable('test_git_checkout_branch',
  ['tests/unit/test_git_checkout_branch.cpp',
   'src/tools/git_checkout_branch/git_checkout_branch_entry.cpp',
   'src/tools/git_checkout_branch/git_checkout_branch_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_checkout_branch', test_git_checkout_branch, suite: 'agent')

test_git_commit = executable('test_git_commit',
  ['tests/unit/test_git_commit.cpp',
   'src/tools/git_commit/git_commit_entry.cpp',
   'src/tools/git_commit/git_commit_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_commit', test_git_commit, suite: 'agent')

test_git_diff_from_branch = executable('test_git_diff_from_branch',
  ['tests/unit/test_git_diff_from_branch.cpp',
   'src/tools/git_diff_from_branch/git_diff_from_branch_entry.cpp',
   'src/tools/git_diff_from_branch/git_diff_from_branch_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_diff_from_branch', test_git_diff_from_branch, suite: 'agent')

test_git_diff_staged = executable('test_git_diff_staged',
  ['tests/unit/test_git_diff_staged.cpp',
   'src/tools/git_diff_staged/git_diff_staged_entry.cpp',
   'src/tools/git_diff_staged/git_diff_staged_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_diff_staged', test_git_diff_staged, suite: 'agent')

test_git_diff_unstaged = executable('test_git_diff_unstaged',
  ['tests/unit/test_git_diff_unstaged.cpp',
   'src/tools/git_diff_unstaged/git_diff_unstaged_entry.cpp',
   'src/tools/git_diff_unstaged/git_diff_unstaged_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_diff_unstaged', test_git_diff_unstaged, suite: 'agent')

test_git_init = executable('test_git_init',
  ['tests/unit/test_git_init.cpp',
   'src/tools/git_init/git_init_entry.cpp',
   'src/tools/git_init/git_init_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_init', test_git_init, suite: 'agent')

test_git_log = executable('test_git_log',
  ['tests/unit/test_git_log.cpp',
   'src/tools/git_log/git_log_entry.cpp',
   'src/tools/git_log/git_log_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_log', test_git_log, suite: 'agent')

test_git_blame = executable('test_git_blame',
  ['tests/unit/test_git_blame.cpp',
   'src/tools/git_blame/git_blame_entry.cpp',
   'src/tools/git_blame/git_blame_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_blame', test_git_blame, suite: 'agent')

test_git_pull = executable('test_git_pull',
  ['tests/unit/test_git_pull.cpp',
   'src/tools/git_pull/git_pull_entry.cpp',
   'src/tools/git_pull/git_pull_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_pull', test_git_pull, suite: 'agent')

test_git_push = executable('test_git_push',
  ['tests/unit/test_git_push.cpp',
   'src/tools/git_push/git_push_entry.cpp',
   'src/tools/git_push/git_push_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_push', test_git_push, suite: 'agent')

test_git_restore = executable('test_git_restore',
  ['tests/unit/test_git_restore.cpp',
   'src/tools/git_restore/git_restore_entry.cpp',
   'src/tools/git_restore/git_restore_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_restore', test_git_restore, suite: 'agent')

test_git_status = executable('test_git_status',
  ['tests/unit/test_git_status.cpp',
   'src/tools/git_status/git_status_entry.cpp',
   'src/tools/git_status/git_status_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_status', test_git_status, suite: 'agent')

test_git_list_files = executable('test_git_list_files',
  ['tests/unit/test_git_list_files.cpp',
   'src/tools/git_list_files/git_list_files_entry.cpp',
   'src/tools/git_list_files/git_list_files_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_list_files', test_git_list_files, suite: 'agent')

test_git_unstage = executable('test_git_unstage',
  ['tests/unit/test_git_unstage.cpp',
   'src/tools/git_unstage/git_unstage_entry.cpp',
   'src/tools/git_unstage/git_unstage_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_git_unstage', test_git_unstage, suite: 'agent')

test_wait_for_subagent = executable('test_wait_for_subagent',
  ['tests/unit/test_wait_for_subagent.cpp',
   'src/tools/wait_for_subagent/wait_for_subagent_entry.cpp',
   'src/tools/wait_for_subagent/wait_for_subagent_security.cpp',
   'src/tools/invoke_subagent/invoke_subagent_entry.cpp',
   'src/tools/invoke_subagent/invoke_subagent_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_wait_for_subagent', test_wait_for_subagent, suite: 'agent')

test_web_fetch = executable('test_web_fetch',
  ['tests/unit/test_web_fetch.cpp',
   'src/tools/web_fetch/web_fetch_entry.cpp',
   'src/tools/web_fetch/web_fetch_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_web_fetch', test_web_fetch, suite: 'agent')



test_ai_model_scoring = executable('test_ai_model_scoring',
  'tests/unit/test_ai_model_scoring.cpp',
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_ai_model_scoring', test_ai_model_scoring, suite: 'agent')

test_ui_paged_container = executable('test_ui_paged_container',
  'tests/unit/test_ui_paged_container.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_ui_paged_container', test_ui_paged_container, suite: 'ui')

test_ui_tabbed_container = executable('test_ui_tabbed_container',
  'tests/unit/test_ui_tabbed_container.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_ui_tabbed_container', test_ui_tabbed_container, suite: 'ui')

test_options_dialogs = executable('test_options_dialogs',
  'tests/unit/test_options_dialogs.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_options_dialogs', test_options_dialogs, suite: 'ui')

test_menu_submenus = executable('test_menu_submenus',
  'tests/unit/test_menu_submenus.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_menu_submenus', test_menu_submenus, suite: 'ui')

test_latency_spike = executable('test_latency_spike',
  'tests/unit/test_latency_spike.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_latency_spike', test_latency_spike, suite: 'agent')





test_agent_compress_history = executable('test_agent_compress_history',
  ['tests/unit/test_agent_compress_history.cpp',
   'src/tools/agent_compress_history/agent_compress_history_entry.cpp',
   'src/tools/agent_compress_history/agent_compress_history_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_compress_history', test_agent_compress_history, is_parallel: false, suite: 'agent')

test_exit_summarization = executable('test_exit_summarization',
  ['tests/unit/test_exit_summarization.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_exit_summarization', test_exit_summarization, suite: 'agent')

# agentlib unit tests added per testplan.md priorities 1, 2, 5
# (llm_client, ai_agent state/admin surface, recording/replay transports)
test_llm_client = executable('test_llm_client',
  ['tests/unit/test_llm_client.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_llm_client', test_llm_client, suite: 'agent')

test_ai_agent_state = executable('test_ai_agent_state',
  ['tests/unit/test_ai_agent_state.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_ai_agent_state', test_ai_agent_state, is_parallel: false, suite: 'agent')

test_record_replay_transports = executable('test_record_replay_transports',
  ['tests/unit/test_record_replay_transports.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_record_replay_transports', test_record_replay_transports, suite: 'agent')

test_invoke_subagent = executable('test_invoke_subagent',
  ['tests/unit/test_invoke_subagent.cpp'],
  dependencies : [agent_dep, core_dep, tools_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_invoke_subagent', test_invoke_subagent, suite: 'agent')

test_agent_window = executable('test_agent_window',
  ['tests/unit/test_agent_window.cpp'],
  dependencies : [agent_dep, core_dep, ui_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_window', test_agent_window, suite: 'agent')

test_agent_center_window = executable('test_agent_center_window',
  ['tests/unit/test_agent_center_window.cpp'],
  dependencies : [agent_dep, core_dep, ui_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_center_window', test_agent_center_window, suite: 'agent')

test_crashdump_window = executable('test_crashdump_window',
  ['tests/unit/test_crashdump_window.cpp'],
  dependencies : [agent_dep, core_dep, ui_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_crashdump_window', test_crashdump_window, suite: 'editor')



test_kill_subagent = executable('test_kill_subagent',
  ['tests/unit/test_kill_subagent.cpp',
   'src/tools/kill_subagent/kill_subagent_entry.cpp',
   'src/tools/kill_subagent/kill_subagent_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_kill_subagent', test_kill_subagent, suite: 'agent')

test_get_subagent_output = executable('test_get_subagent_output',
  ['tests/unit/test_get_subagent_output.cpp',
   'src/tools/get_subagent_output/get_subagent_output_entry.cpp',
   'src/tools/get_subagent_output/get_subagent_output_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_get_subagent_output', test_get_subagent_output, suite: 'agent')

test_report_final_result = executable('test_report_final_result',
  ['tests/unit/test_report_final_result.cpp',
   'src/tools/report_final_result/report_final_result_entry.cpp',
   'src/tools/report_final_result/report_final_result_security.cpp',
   'src/tools/get_subagent_output/get_subagent_output_entry.cpp',
   'src/tools/get_subagent_output/get_subagent_output_security.cpp',
   'src/tools/wait_for_subagent/wait_for_subagent_entry.cpp',
   'src/tools/wait_for_subagent/wait_for_subagent_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_report_final_result', test_report_final_result, suite: 'agent')

test_api_formatter = executable('test_api_formatter',
  ['tests/unit/test_api_formatter.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_api_formatter', test_api_formatter, suite: 'agent')

test_agent_get_run_screenshot = executable('test_agent_get_run_screenshot',
  ['tests/unit/test_agent_get_run_screenshot.cpp',
   'src/tools/agent_get_run_screenshot/agent_get_run_screenshot_entry.cpp',
   'src/tools/agent_get_run_screenshot/agent_get_run_screenshot_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_get_run_screenshot', test_agent_get_run_screenshot, suite: 'agent')

test_list_subagents = executable('test_list_subagents',
  ['tests/unit/test_list_subagents.cpp',
   'src/tools/list_subagents/list_subagents_entry.cpp',
   'src/tools/list_subagents/list_subagents_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_list_subagents', test_list_subagents, suite: 'agent')



test_agent_mark_episode = executable('test_agent_mark_episode',
  ['tests/unit/test_agent_mark_episode.cpp',
   'src/tools/agent_mark_episode/agent_mark_episode_entry.cpp',
   'src/tools/agent_mark_episode/agent_mark_episode_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_mark_episode', test_agent_mark_episode, suite: 'agent')

test_agent_set_status = executable('test_agent_set_status',
  ['tests/unit/test_agent_set_status.cpp',
   'src/tools/agent_set_status/agent_set_status_entry.cpp',
   'src/tools/agent_set_status/agent_set_status_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_set_status', test_agent_set_status, suite: 'agent')

test_agent_start_app = executable('test_agent_start_app',
  ['tests/unit/test_agent_start_app.cpp',
   'src/tools/agent_start_app/agent_start_app_entry.cpp',
   'src/tools/agent_start_app/agent_start_app_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_start_app', test_agent_start_app, suite: 'agent')

test_agent_debug_coredump = executable('test_agent_debug_coredump',
  ['tests/unit/test_agent_debug_coredump.cpp',
   'src/tools/agent_debug_coredump/agent_debug_coredump_entry.cpp',
   'src/tools/agent_debug_coredump/agent_debug_coredump_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_debug_coredump', test_agent_debug_coredump, suite: 'agent')

test_agent_set_application_binary = executable('test_agent_set_application_binary',
  ['tests/unit/test_agent_set_application_binary.cpp',
   'src/tools/agent_set_application_binary/agent_set_application_binary_entry.cpp',
   'src/tools/agent_set_application_binary/agent_set_application_binary_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_set_application_binary', test_agent_set_application_binary, suite: 'agent')

test_get_subagent_status = executable('test_get_subagent_status',
  ['tests/unit/test_get_subagent_status.cpp',
   'src/tools/get_subagent_status/get_subagent_status_entry.cpp',
   'src/tools/get_subagent_status/get_subagent_status_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_get_subagent_status', test_get_subagent_status, suite: 'agent')

test_agent_terminate_run = executable('test_agent_terminate_run',
  ['tests/unit/test_agent_terminate_run.cpp',
   'src/tools/agent_terminate_run/agent_terminate_run_entry.cpp',
   'src/tools/agent_terminate_run/agent_terminate_run_security.cpp',
   'src/perf_manager.cpp',
   'src/address_lookup.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_terminate_run', test_agent_terminate_run, suite: 'agent')

test_agent_wait_for_app = executable('test_agent_wait_for_app',
  ['tests/unit/test_agent_wait_for_app.cpp',
   'src/tools/agent_wait_for_app/agent_wait_for_app_entry.cpp',
   'src/tools/agent_wait_for_app/agent_wait_for_app_security.cpp',
   'src/perf_manager.cpp',
   'src/address_lookup.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_wait_for_app', test_agent_wait_for_app, suite: 'agent')



test_agent_write_to_run = executable('test_agent_write_to_run',
  ['tests/unit/test_agent_write_to_run.cpp',
   'src/tools/agent_write_to_run/agent_write_to_run_entry.cpp',
   'src/tools/agent_write_to_run/agent_write_to_run_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_agent_write_to_run', test_agent_write_to_run, suite: 'agent')

test_ask_user = executable('test_ask_user',
  ['tests/unit/test_ask_user.cpp',
   'src/tools/ask_user/ask_user_entry.cpp',
   'src/tools/ask_user/ask_user_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_ask_user', test_ask_user, suite: 'agent')

test_clear_all_errors = executable('test_clear_all_errors',
  ['tests/unit/test_clear_all_errors.cpp',
   'src/tools/clear_all_errors/clear_all_errors_entry.cpp',
   'src/tools/clear_all_errors/clear_all_errors_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_clear_all_errors', test_clear_all_errors, suite: 'agent')

test_code_get_definition = executable('test_code_get_definition',
  ['tests/unit/test_code_get_definition.cpp',
   'src/tools/code_get_definition/code_get_definition_entry.cpp',
   'src/tools/code_get_definition/code_get_definition_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_code_get_definition', test_code_get_definition, suite: 'agent')

test_code_get_references = executable('test_code_get_references',
  ['tests/unit/test_code_get_references.cpp',
   'src/tools/code_get_references/code_get_references_entry.cpp',
   'src/tools/code_get_references/code_get_references_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_code_get_references', test_code_get_references, suite: 'agent')

test_code_get_scope = executable('test_code_get_scope',
  ['tests/unit/test_code_get_scope.cpp',
   'src/tools/code_get_scope/code_get_scope_entry.cpp',
   'src/tools/code_get_scope/code_get_scope_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_code_get_scope', test_code_get_scope, suite: 'agent')

test_crashdump_get_info = executable('test_crashdump_get_info',
  ['tests/unit/test_crashdump_get_info.cpp',
   'src/tools/crashdump_get_info/crashdump_get_info_entry.cpp',
   'src/tools/crashdump_get_info/crashdump_get_info_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_crashdump_get_info', test_crashdump_get_info, suite: 'agent')

test_assert_fail = executable('test_assert_fail',
  'tests/unit/test_assert_fail.cpp',
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_assert_fail', test_assert_fail, suite: 'agent', depends: libturbocatch, env: ['TURBOSTAR_TURBOCATCH_DIR=' + meson.project_build_root()])

test_watchdog_assert = executable('test_watchdog_assert',
  'tests/unit/test_watchdog_assert.cpp',
  dependencies : [libunwind_dep],
  include_directories: inc)
test('unit_test_watchdog_assert', test_watchdog_assert, suite: 'agent')

test_primary_language_setting = executable('test_primary_language_setting',
  ['tests/unit/test_primary_language_setting.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_primary_language_setting', test_primary_language_setting, suite: 'agent')

test_system_vfs = executable('test_system_vfs',
  ['tests/unit/test_system_vfs.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_system_vfs', test_system_vfs, suite: 'agent')

test_fallback_crash = executable('test_fallback_crash',
  ['tests/unit/test_fallback_crash.cpp', 'src/crash_handler.cpp', git_version_h],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fallback_crash', test_fallback_crash, should_fail: true, suite: 'agent')

test_crashdump_list = executable('test_crashdump_list',
  ['tests/unit/test_crashdump_list.cpp',
   'src/tools/crashdump_list/crashdump_list_entry.cpp',
   'src/tools/crashdump_list/crashdump_list_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_crashdump_list', test_crashdump_list, suite: 'agent')

test_flag_as_error = executable('test_flag_as_error',
  ['tests/unit/test_flag_as_error.cpp',
   'src/tools/flag_as_error/flag_as_error_entry.cpp',
   'src/tools/flag_as_error/flag_as_error_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_flag_as_error', test_flag_as_error, suite: 'agent')

test_fs_list_dir = executable('test_fs_list_dir',
  ['tests/unit/test_fs_list_dir.cpp',
   'src/tools/fs_list_dir/fs_list_dir_entry.cpp',
   'src/tools/fs_list_dir/fs_list_dir_security.cpp',
   'src/codemap_utils.cpp',
   'src/tools/fs_file_size/fs_file_size_entry.cpp',
   'src/tools/fs_file_size/fs_file_size_security.cpp',
   'src/tools/fs_grep_files/fs_grep_files_entry.cpp',
   'src/tools/fs_grep_files/fs_grep_files_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, magic_dep],
  include_directories: inc)
test('unit_test_fs_list_dir', test_fs_list_dir, suite: 'agent')

test_fs_glob = executable('test_fs_glob',
  ['tests/unit/test_fs_glob.cpp',
   'src/tools/fs_glob/fs_glob_entry.cpp',
   'src/tools/fs_glob/fs_glob_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_glob', test_fs_glob, suite: 'agent')

test_fs_replace_content = executable('test_fs_replace_content',
  ['tests/unit/test_fs_replace_content.cpp',
   'src/tools/fs_replace_content/fs_replace_content_entry.cpp',
   'src/tools/fs_replace_content/fs_replace_content_security.cpp',
   'src/tools/fs_replace_content/fs_replace_engine.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_replace_content', test_fs_replace_content, suite: 'agent')

test_fs_multi_replace_content = executable('test_fs_multi_replace_content',
  ['tests/unit/test_fs_multi_replace_content.cpp',
   'src/tools/fs_replace_content/fs_replace_content_entry.cpp',
   'src/tools/fs_replace_content/fs_replace_content_security.cpp',
   'src/tools/fs_replace_content/fs_multi_replace_content_entry.cpp',
   'src/tools/fs_replace_content/fs_multi_replace_content_security.cpp',
   'src/tools/fs_replace_content/fs_replace_engine.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_multi_replace_content', test_fs_multi_replace_content, suite: 'agent')

test_fs_mkdir = executable('test_fs_mkdir',
  ['tests/unit/test_fs_mkdir.cpp',
   'src/tools/fs_mkdir/fs_mkdir_entry.cpp',
   'src/tools/fs_mkdir/fs_mkdir_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_mkdir', test_fs_mkdir, suite: 'agent')

test_fs_read_binary = executable('test_fs_read_binary',
  ['tests/unit/test_fs_read_binary.cpp',
   'src/tools/fs_read_binary/fs_read_binary_entry.cpp',
   'src/tools/fs_read_binary/fs_read_binary_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, magic_dep],
  include_directories: inc)
test('unit_test_fs_read_binary', test_fs_read_binary, suite: 'agent')

test_fs_regexp_lines = executable('test_fs_regexp_lines',
  ['tests/unit/test_fs_regexp_lines.cpp',
   'src/tools/fs_regexp_lines/fs_regexp_lines_entry.cpp',
   'src/tools/fs_regexp_lines/fs_regexp_lines_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_regexp_lines', test_fs_regexp_lines, suite: 'agent')

test_fs_compile_file = executable('test_fs_compile_file',
  ['tests/unit/test_fs_compile_file.cpp',
   'src/tools/fs_compile_file/fs_compile_file_entry.cpp',
   'src/tools/fs_compile_file/fs_compile_file_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_compile_file', test_fs_compile_file, suite: 'agent')

test_fs_compile_info = executable('test_fs_compile_info',
  ['tests/unit/test_fs_compile_info.cpp',
   'src/tools/fs_compile_info/fs_compile_info_entry.cpp',
   'src/tools/fs_compile_info/fs_compile_info_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_compile_info', test_fs_compile_info, suite: 'agent')

test_fs_compile_project = executable('test_fs_compile_project',
  ['tests/unit/test_fs_compile_project.cpp',
   'src/tools/fs_compile_project/fs_compile_project_entry.cpp',
   'src/tools/fs_compile_project/fs_compile_project_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_fs_compile_project', test_fs_compile_project, suite: 'agent')

test_get_current_datetime = executable('test_get_current_datetime',
  ['tests/unit/test_get_current_datetime.cpp',
   'src/tools/get_current_datetime/get_current_datetime_entry.cpp',
   'src/tools/get_current_datetime/get_current_datetime_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_get_current_datetime', test_get_current_datetime, suite: 'agent')


test_open_in_editor = executable('test_open_in_editor',
  ['tests/unit/test_open_in_editor.cpp',
   'src/tools/open_in_editor/open_in_editor_entry.cpp',
   'src/tools/open_in_editor/open_in_editor_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_open_in_editor', test_open_in_editor, suite: 'agent')

test_vim_emulation = executable('test_vim_emulation',
  'tests/unit/test_vim_emulation.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_test_vim_emulation', test_vim_emulation, suite: 'agent')

test_file_security = executable('test_file_security',
  ['tests/unit/test_file_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, thread_dep, nlohmann_json_dep, curl_dep],
  include_directories: inc)
test('unit_file_security', test_file_security, suite: 'agent')
test_fs_replace_lines = executable('test_fs_replace_lines',
  ['tests/unit/test_fs_replace_lines.cpp', 'src/tools/fs_replace_lines/fs_replace_lines_entry.cpp', 'src/tools/fs_replace_lines/fs_replace_lines_security.cpp'],
  dependencies : [agent_dep, core_dep, nlohmann_json_dep, httplib_dep, thread_dep, curl_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: 'src')
test('unit_fs_replace_lines', test_fs_replace_lines, suite: 'agent')

test_fs_file_codemap = executable('test_fs_file_codemap',
  ['tests/unit/test_fs_file_codemap.cpp', 'src/codemap_utils.cpp', 'src/tools/fs_file_codemap/fs_file_codemap_entry.cpp', 'src/tools/fs_file_codemap/fs_file_codemap_security.cpp', 'src/tools/fs_read_lines/fs_read_lines_entry.cpp', 'src/tools/fs_read_lines/fs_read_lines_security.cpp'],
  dependencies : [agent_dep, core_dep, nlohmann_json_dep, httplib_dep, thread_dep, curl_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: 'src')
test('unit_fs_file_codemap', test_fs_file_codemap, suite: 'agent')

test_virtual_file_system = executable('test_virtual_file_system',
  ['tests/unit/test_virtual_file_system.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, thread_dep, nlohmann_json_dep, curl_dep],
  include_directories: inc)
test('unit_virtual_file_system', test_virtual_file_system, suite: 'agent')

test_mcp_manager = executable('test_mcp_manager',
  'tests/unit/test_mcp_manager.cpp',
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_mcp_manager', test_mcp_manager, timeout: 60, suite: 'agent')

test_listbox = executable('test_listbox',
  'tests/unit/test_listbox.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_listbox', test_listbox, suite: 'editor')

test_input_history = executable('test_input_history',
  'tests/unit/test_input_history.cpp',
  dependencies : [agent_dep, core_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_input_history', test_input_history, suite: 'editor')

test_grid_flow = executable('test_grid_flow',
  'tests/unit/test_grid_flow.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_grid_flow', test_grid_flow, suite: 'editor')

test_durmovie = executable('test_durmovie',
  'tests/unit/test_durmovie.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_durmovie', test_durmovie, suite: 'editor')

test_agent_animation = executable('test_agent_animation',
  'tests/unit/test_agent_animation.cpp',
  dependencies : [agent_dep, core_dep, nlohmann_json_dep, thread_dep],
  include_directories: inc)
test('unit_agent_animation', test_agent_animation, suite: 'agent')

test_image_manager = executable('test_image_manager',
  'tests/unit/test_image_manager.cpp',
  dependencies : [agent_dep, core_dep, nlohmann_json_dep, thread_dep, openssl_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_image_manager', test_image_manager, suite: 'agent')

test_image_tools = executable('test_image_tools',
  'tests/unit/test_image_tools.cpp',
  dependencies : [tools_dep, agent_dep, core_dep, nlohmann_json_dep, thread_dep, openssl_dep, lsp_dep, re2_dep, sqlite3_dep, curl_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true)
test('unit_image_tools', test_image_tools, suite: 'agent',
  depends: [image_basic_plugin],
  env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])


test_agent_tile = executable('test_agent_tile',
  'tests/unit/test_agent_tile.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_agent_tile', test_agent_tile, suite: 'editor')

test_ui_color_picker = executable('test_ui_color_picker',
  'tests/unit/test_ui_color_picker.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_ui_color_picker', test_ui_color_picker, suite: 'editor')

test_syntax_colors_dialog = executable('test_syntax_colors_dialog',
  'tests/unit/test_syntax_colors_dialog.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_syntax_colors_dialog', test_syntax_colors_dialog, suite: 'editor')

test_button_hotkeys = executable('test_button_hotkeys',
  'tests/unit/test_button_hotkeys.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_button_hotkeys', test_button_hotkeys, suite: 'editor')

test_ui_thumbnail = executable('test_ui_thumbnail',
  'tests/unit/test_ui_thumbnail.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_ui_thumbnail', test_ui_thumbnail, suite: 'editor')

test_syntax_color_manager = executable('test_syntax_color_manager',
  'tests/unit/test_syntax_color_manager.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_syntax_color_manager', test_syntax_color_manager, suite: 'editor')

test_multiline_edit = executable('test_multiline_edit',
  'tests/unit/test_multiline_edit.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_multiline_edit', test_multiline_edit, suite: 'editor')

test_github_vfs = executable('test_github_vfs',
  ['tests/unit/test_github_vfs.cpp'],
  dependencies : [agent_dep, core_dep, lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, httplib_dep, thread_dep, curl_dep],
  include_directories: inc)
test('unit_github_vfs', test_github_vfs, suite: 'agent')

test_skill_manager = executable('test_skill_manager',
  ['tests/unit/test_skill_manager.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, thread_dep, nlohmann_json_dep, curl_dep, re2_dep, yaml_cpp_dep],
  include_directories: inc)
test('unit_skill_manager', test_skill_manager, suite: 'agent')

test_subagent_manager = executable('test_subagent_manager',
  ['tests/unit/test_subagent_manager.cpp'],
  dependencies : [agent_dep, core_dep],
  include_directories: 'src')
test('unit_subagent_manager', test_subagent_manager, suite: 'agent')


test_python_highlighter = executable('test_python_highlighter',
  ['tests/unit/test_python_highlighter.cpp', 'src/highlighter/python_highlighter.cpp', 'src/line.cpp', 'src/utf8.cpp', 'src/mime.cpp'],
  dependencies : [re2_dep],
  include_directories: 'src')
test('unit_python_highlighter', test_python_highlighter, suite: 'editor')

test_verilog_highlighter = executable('test_verilog_highlighter',
  ['tests/unit/test_verilog_highlighter.cpp', 'src/highlighter/verilog_highlighter.cpp', 'src/line.cpp', 'src/utf8.cpp', 'src/mime.cpp'],
  dependencies : [re2_dep],
  include_directories: 'src')
test('unit_verilog_highlighter', test_verilog_highlighter, suite: 'editor')

test_cpp_highlighter = executable('test_cpp_highlighter',
  ['tests/unit/test_cpp_highlighter.cpp', 'src/highlighter/cpp_highlighter.cpp', 'src/line.cpp', 'src/utf8.cpp', 'src/mime.cpp'],
  dependencies : [re2_dep],
  include_directories: 'src')
test('unit_cpp_highlighter', test_cpp_highlighter, suite: 'editor')

test_markdown_highlighter = executable('test_markdown_highlighter',
  ['tests/unit/test_markdown_highlighter.cpp', 'src/highlighter/markdown_highlighter.cpp', 'src/line.cpp', 'src/markdown_utils.cpp', 'src/utf8.cpp', 'src/mime.cpp'],
  dependencies : [re2_dep],
  include_directories: 'src')
test('unit_markdown_highlighter', test_markdown_highlighter, suite: 'editor')

test_html_highlighter = executable('test_html_highlighter',
  ['tests/unit/test_html_highlighter.cpp', 'src/highlighter/html_highlighter.cpp', 'src/line.cpp', 'src/utf8.cpp', 'src/mime.cpp'],
  dependencies : [],
  include_directories: 'src')
test('unit_html_highlighter', test_html_highlighter, suite: 'editor')

test_json_highlighter = executable('test_json_highlighter',
  ['tests/unit/test_json_highlighter.cpp', 'src/highlighter/json_highlighter.cpp', 'src/line.cpp', 'src/utf8.cpp', 'src/mime.cpp'],
  dependencies : [re2_dep],
  include_directories: 'src')
test('unit_json_highlighter', test_json_highlighter, suite: 'editor')



test_markdown_utils = executable('test_markdown_utils',
  ['tests/unit/test_markdown_utils.cpp', 'src/markdown_utils.cpp', 'src/utf8.cpp', 'src/mime.cpp'],
  dependencies : [re2_dep],
  include_directories: 'src')
test('unit_markdown_utils', test_markdown_utils, suite: 'editor')

test_utf8 = executable('test_utf8',
  ['tests/unit/test_utf8.cpp', 'src/utf8.cpp', 'src/mime.cpp'],
  dependencies : [],
  include_directories: 'src')
test('unit_utf8', test_utf8, suite: 'editor')

test_mime = executable('test_mime',
  ['tests/unit/test_mime.cpp', 'src/mime.cpp', 'src/utf8.cpp'],
  dependencies : [],
  include_directories: 'src')
test('unit_mime', test_mime, suite: 'editor')

test_context_dnn = executable('test_context_dnn',
  'tests/unit/test_context_dnn.cpp',
  dependencies : [agent_dep, core_dep, nlohmann_json_dep, thread_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: inc)
test('unit_context_dnn', test_context_dnn, suite: 'agent')

test_document = executable('test_document',
  'tests/unit/test_document.cpp',
  dependencies : [core_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: inc)
test('unit_document', test_document, suite: 'editor')

test_document_reload = executable('test_document_reload',
  'tests/unit/test_document_reload.cpp',
  dependencies : [core_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: inc)
test('unit_document_reload', test_document_reload, suite: 'editor')

test_process_runner = executable('test_process_runner',
  'tests/unit/test_process_runner.cpp',
  dependencies : [core_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: inc)
test('unit_process_runner', test_process_runner, suite: 'editor')


test_crash_handler_race = executable('test_crash_handler_race',
  ['tests/unit/test_crash_handler_race.cpp', 'src/crash_handler.cpp', git_version_h],
  dependencies : [core_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep, libunwind_dep],
  include_directories: inc)
test('unit_crash_handler_race', test_crash_handler_race, suite: 'editor')

test_debugger_detection = executable('test_debugger_detection',
  ['tests/unit/test_debugger_detection.cpp', 'src/crash_handler.cpp', git_version_h],
  dependencies : [core_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep, libunwind_dep],
  include_directories: inc)
test('unit_debugger_detection', test_debugger_detection, suite: 'editor')

test_window_scroll = executable('test_window_scroll',
  'tests/unit/test_window_scroll.cpp',
  dependencies : [agent_dep, core_dep, ui_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_window_scroll', test_window_scroll, suite: 'editor')

test_gcc_log_parser = executable('test_gcc_log_parser',
  'tests/unit/test_gcc_log_parser.cpp',
  dependencies : [core_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: inc)
test('unit_gcc_log_parser', test_gcc_log_parser, suite: 'editor')

test_line = executable('test_line',
  'tests/unit/test_line.cpp',
  dependencies : [core_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: inc)
test('unit_line', test_line, suite: 'editor')

test_hex_highlighter = executable('test_hex_highlighter',
  'tests/unit/test_hex_highlighter.cpp',
  dependencies : [core_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep],
  include_directories: inc)
test('unit_hex_highlighter', test_hex_highlighter, suite: 'editor')




test_sqlite_validation = executable('test_sqlite_validation',
  'tests/unit/test_sqlite_validation.cpp',
  dependencies : [agent_dep, core_dep, nlohmann_json_dep, re2_dep, lsp_dep, httplib_dep, thread_dep, curl_dep, yaml_cpp_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true)
test('unit_sqlite_validation', test_sqlite_validation, suite: 'agent',
  depends: [sqlite_plugin],
  env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])

test_crashdump_clear = executable('test_crashdump_clear',
  ['tests/unit/test_crashdump_clear.cpp', 'src/tools/crashdump_clear/crashdump_clear_entry.cpp'],
  dependencies : [agent_dep, core_dep, lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, httplib_dep, thread_dep, curl_dep, dl_dep],
  include_directories: 'src')
test('unit_crashdump_clear', test_crashdump_clear, suite: 'agent')



test_httplib_transport = executable('test_httplib_transport',
  ['tests/unit/test_httplib_transport.cpp'],
  dependencies : [agent_dep, core_dep, lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, httplib_dep, thread_dep, curl_dep],
  include_directories: 'src')
test('unit_httplib_transport', test_httplib_transport, suite: 'agent')

test_copilot_models = executable('test_copilot_models',
  ['tests/unit/test_copilot_models.cpp'],
  dependencies : [agent_dep, core_dep, lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, httplib_dep, thread_dep, curl_dep],
  include_directories: 'src')
test('unit_copilot_models', test_copilot_models, suite: 'agent')

test_model_server = executable('test_model_server',
  ['tests/unit/test_model_server.cpp'],
  dependencies : [agent_dep, core_dep, lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep, httplib_dep, thread_dep, curl_dep],
  include_directories: 'src')
test('unit_model_server', test_model_server, suite: 'agent')

test_fs_grep_files = executable('test_fs_grep_files',
  ['tests/unit/test_fs_grep_files.cpp', 'src/tools/fs_grep_files/fs_grep_files_entry.cpp', 'src/tools/fs_grep_files/fs_grep_files_security.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_fs_grep_files', test_fs_grep_files, suite: 'agent')

test_project_layout = executable('test_project_layout',
  ['tests/unit/test_project_layout.cpp'],
  dependencies : [agent_dep, core_dep, lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep],
  include_directories: 'src')
test('unit_project_layout', test_project_layout, suite: 'agent')

test_available_tests = executable('test_available_tests',
  ['tests/unit/test_available_tests.cpp'],
  dependencies : [agent_dep, core_dep, lsp_dep, nlohmann_json_dep, sqlite3_dep, re2_dep],
  include_directories: 'src')
test('unit_test_available_tests', test_available_tests, is_parallel: false, suite: 'agent')

test_output_filter = executable('test_output_filter',
  ['tests/unit/test_output_filter.cpp'],
  include_directories: 'src')
test('unit_output_filter', test_output_filter, suite: 'editor')

test_run_config = executable('test_run_config',
  'tests/unit/test_run_config.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_run_config', test_run_config, suite: 'editor')

test_session_manager = executable('test_session_manager',
  'tests/unit/test_session_manager.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_session_manager', test_session_manager, suite: 'editor')

test_tool_status = executable('test_tool_status',
  'tests/unit/test_tool_status.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_tool_status', test_tool_status, suite: 'editor')

test_codereview_manager = executable('test_codereview_manager',
  'tests/unit/test_codereview_manager.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_codereview_manager', test_codereview_manager, suite: 'agent')

test_code_review_window = executable('test_code_review_window',
  'tests/unit/test_code_review_window.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_code_review_window', test_code_review_window, suite: 'agent')

test_codereview_line_tracking = executable('test_codereview_line_tracking',
  'tests/unit/test_codereview_line_tracking.cpp',
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_codereview_line_tracking', test_codereview_line_tracking, suite: 'agent')

test_create_code_review_item = executable('test_create_code_review_item',
  ['tests/unit/test_create_code_review_item.cpp',
   'src/tools/create_code_review_item/create_code_review_item_entry.cpp',
   'src/tools/create_code_review_item/create_code_review_item_security.cpp'],
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_create_code_review_item', test_create_code_review_item, suite: 'agent')

test_update_code_review_item = executable('test_update_code_review_item',
  ['tests/unit/test_update_code_review_item.cpp',
   'src/tools/update_code_review_item/update_code_review_item_entry.cpp',
   'src/tools/update_code_review_item/update_code_review_item_security.cpp'],
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_update_code_review_item', test_update_code_review_item, suite: 'agent')

test_confirm_code_review_item = executable('test_confirm_code_review_item',
  ['tests/unit/test_confirm_code_review_item.cpp',
   'src/tools/confirm_code_review_item/confirm_code_review_item_entry.cpp',
   'src/tools/confirm_code_review_item/confirm_code_review_item_security.cpp'],
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_confirm_code_review_item', test_confirm_code_review_item, suite: 'agent')

test_resolve_code_review_item = executable('test_resolve_code_review_item',
  ['tests/unit/test_resolve_code_review_item.cpp',
   'src/tools/resolve_code_review_item/resolve_code_review_item_entry.cpp',
   'src/tools/resolve_code_review_item/resolve_code_review_item_security.cpp'],
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_resolve_code_review_item', test_resolve_code_review_item, suite: 'agent')

test_list_code_review_items = executable('test_list_code_review_items',
  ['tests/unit/test_list_code_review_items.cpp',
   'src/tools/list_code_review_items/list_code_review_items_entry.cpp',
   'src/tools/list_code_review_items/list_code_review_items_security.cpp'],
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_list_code_review_items', test_list_code_review_items, suite: 'agent')

test_get_code_review_item = executable('test_get_code_review_item',
  ['tests/unit/test_get_code_review_item.cpp',
   'src/tools/get_code_review_item/get_code_review_item_entry.cpp',
   'src/tools/get_code_review_item/get_code_review_item_security.cpp'],
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_get_code_review_item', test_get_code_review_item, suite: 'agent')

test_perform_code_review = executable('test_perform_code_review',
  ['tests/unit/test_perform_code_review.cpp',
   'src/tools/perform_code_review/perform_code_review_entry.cpp',
   'src/tools/perform_code_review/perform_code_review_security.cpp'],
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc,
  export_dynamic: true)
test('unit_perform_code_review', test_perform_code_review, suite: 'agent', is_parallel: false, env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins'])

test_editor_tool_family = executable('test_editor_tool_family',
  ['tests/unit/test_editor_tool_family.cpp'],
  dependencies : [tools_dep, agent_dep, core_dep, httplib_dep, thread_dep, nlohmann_json_dep],
  include_directories: inc)
test('unit_editor_tool_family', test_editor_tool_family, suite: 'agent')

test_sysprompt_command = executable('test_sysprompt_command',
  ['tests/unit/test_sysprompt_command.cpp'],
  dependencies : [agent_dep, core_dep, httplib_dep, thread_dep, nlohmann_json_dep],
  include_directories: inc)
test('unit_sysprompt_command', test_sysprompt_command, suite: 'agent')

test_security_review_with_agent = executable('test_security_review_with_agent',
  ['tests/unit/test_security_review_with_agent.cpp',
   'src/plugins/securityagent/security_review_with_agent/security_review_with_agent_entry.cpp',
   'src/plugins/securityagent/security_review_with_agent/security_review_with_agent_security.cpp'],
  dependencies : [ui_dep, agent_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep],
  include_directories: inc)
test('unit_security_review_with_agent', test_security_review_with_agent, suite: 'agent')






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
    'UV_OFFLINE=1',
    'UV_CACHE_DIR=' + meson.project_source_root() / '.turbostar' / 'uv_cache',
    'TURBOSTAR_IN_TESTSUITE=1',
    'TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins',
    'TURBOSTAR_TURBOCATCH_DIR=' + meson.project_build_root()
  ]
  e2e_depends = [turbostar_exe, agentcli_record_exe, agentcli_replay_exe, copy_to_testrun_tgt, libturbocatch]

  test('e2e_context_paging', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_context_paging.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 180,
    is_parallel: false,
    suite: 'agent'
  )
  test('e2e_exit_delay', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_exit_delay.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'agent'
  )
  test('e2e_turboagent_startup', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_turboagent_startup.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false,
    suite: 'agent'
  )
  test('e2e_basic', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_basic.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_k_block_help', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_k_block_help.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_hex_editor', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_hex_editor.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_mouse_copy', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_mouse_copy.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false,
    suite: 'editor'
  )
  test('e2e_disk_change', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_disk_change.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_readonly_help', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_readonly_help.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_menus', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_menus.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_file_close_menu', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_file_close_menu.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_cursor', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_cursor.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_cursor_wrap', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_cursor_wrap.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_typing', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_typing.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_utf8', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_utf8.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_backspace', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_backspace.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_enter', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_enter.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_delete_line', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_delete_line.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_compaction', e2e_exe,
    args: e2e_base_args + ['-m', 'pytest', join_paths(meson.project_source_root(), 'tests/e2e/test_compaction.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'agent'
  )
  test('e2e_selection', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_selection.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_power_nav', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_power_nav.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_undo_redo', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_undo_redo.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_insert_file', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_insert_file.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_format', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_format.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_settings', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_settings.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_bracket_matching', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_bracket_matching.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_scope_selection', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_scope_selection.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_multiple_filenames', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_multiple_filenames.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  if git_prog.found()
    test('e2e_git', e2e_exe,
      args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_git.py')],
      env: e2e_env,
    depends: e2e_depends,
      workdir: meson.project_build_root(),
      timeout: 60,
      is_parallel: false,
      suite: 'agent'
    )
    test('e2e_git_mouse', e2e_exe,
      args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_git_mouse.py')],
      env: e2e_env,
    depends: e2e_depends,
      workdir: meson.project_build_root(),
      timeout: 60,
      is_parallel: false,
      suite: 'agent'
    )
  endif

  test('e2e_save_mouse', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_save_mouse.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_mouse_scroll_zorder', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_mouse_scroll_zorder.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_window_drag_resize', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_window_drag_resize.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_window_maximize', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_window_maximize.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_window_popup', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_window_popup.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_close_dirty', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_close_dirty.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_untitled_save_loop', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_untitled_save_loop.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_force_quit', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_force_quit.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_block_copy_move', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_block_copy_move.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_block_move_down', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_block_move_down.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_block_write', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_block_write.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_word_line_edit', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_word_line_edit.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_content_assertion', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_content_assertion.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_dialog_esc', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_dialog_esc.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_crash_dialog', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_crash_dialog.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_dialogs', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_dialogs.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_image_manager', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_image_manager.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_menu_dialogs', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_menu_dialogs.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_menu_windows', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_menu_windows.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_close_window_crash', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_close_window_crash.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_agent_window_single', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_window_single.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'agent'
  )
  test('e2e_lsp_missing_server', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_lsp_missing_server.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_extra_keys', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_extra_keys.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_search', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_search.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_search_autocomplete', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_search_autocomplete.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_replace', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_replace.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_file_dialog', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_file_dialog.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_advanced_search', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_advanced_search.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_ghost_x', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_ghost_x.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_tabs', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_tabs.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_syntax', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_syntax.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_syntax_markdown', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_syntax_markdown.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_ctrl_sequences', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_ctrl_sequences.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_block_delete', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_block_delete.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_format_paragraph', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_format_paragraph.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_elf_tools_replay', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_elf_tools_replay.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false,
    suite: 'plugins'
  )
  test('e2e_agent_create', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_create.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false,
    suite: 'agent'
  )

  test('e2e_agent_mouse_copy', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_agent_mouse_copy.py')],
    env: e2e_env + ['TURBOSTAR_SKIP_FLAKY=1'],
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'agent'
  )
  test('e2e_double_click_copy', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_double_click_copy.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_exit_save_prompt_loop', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_exit_save_prompt_loop.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false,
    suite: 'editor'
  )
  test('e2e_modal_dialogs', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_modal_dialogs.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )
  test('e2e_sqlite_tools', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_sqlite_tools.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'agent'
  )

  test('e2e_debugger', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_debugger.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_linked_windows', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_linked_windows.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_ncurses_stuck', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_ncurses_stuck.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    suite: 'editor'
  )

  test('e2e_lsp_selection', e2e_exe,
    args: e2e_base_args + [join_paths(meson.project_source_root(), 'tests/e2e/test_lsp_selection.py')],
    env: e2e_env,
    depends: e2e_depends,
    workdir: meson.project_build_root(),
    timeout: 60,
    is_parallel: false,
    suite: 'editor'
  )
endif

tidy_prog = find_program('tidy', required: false)
if tidy_prog.found()
  html_tests = [
    ['index', 'docs/index.html'],
    ['editor', 'docs/editor.html'],
    ['ai', 'docs/ai.html'],
    ['details', 'docs/details.html']
  ]
  foreach item : html_tests
    test_name = 'website_validate_' + item[0]
    html_path = item[1]
    test(test_name, embed_prog,
      args: [join_paths(meson.project_source_root(), 'scripts/verify_html.py'), join_paths(meson.project_source_root(), html_path)],
      suite: 'website'
    )
  endforeach
endif
