subdir('tools')

core_sources = files(
  'line.cpp',
  'document.cpp',
  'binary_document.cpp',
  'document_nav.cpp',
  'document_undo.cpp',
  'document_selection.cpp',
  'document_edit.cpp',
  'document_search.cpp',
  'document_format.cpp',
  'document_highlight.cpp',
  'fs_utils.cpp',
  'config_manager.cpp',
  'statistics_manager.cpp',
  'codereview_manager.cpp',
  'git_manager.cpp',
  'lsp_manager.cpp',
  'project_manager.cpp',
  'event_logger.cpp',
  'event_queue.cpp',
  'gcc_log_parser.cpp',
  'build_error_manager.cpp',
  'crashdump_manager.cpp',
  'process_runner.cpp',
  'markdown_utils.cpp',
  'command_runner.cpp',
  'syntax_color_manager.cpp',
  'highlighter/cpp_highlighter.cpp',
  'highlighter/python_highlighter.cpp',
  'highlighter/markdown_highlighter.cpp',
  'highlighter/verilog_highlighter.cpp',
  'highlighter/html_highlighter.cpp',
  'highlighter/json_highlighter.cpp',
  'highlighter/highlighter_registry.cpp',
  'hex/elf.cpp',
  'hex/png.cpp',
  'hex/jpeg.cpp',
  'hex/hex_highlighter_registry.cpp',
  'history_manager.cpp',
  'input_history_manager.cpp',
  'utf8.cpp',
  'crash_handler.cpp',
  'ansi.cpp',
  'pluginloader.cpp',
  'images/image_manager.cpp',
  'tools/troff2md.cpp'
)



agentlib_sources = files(
  'filter_registry.cpp',
  'agentlib/command_registry.cpp',
  'agentlib/ai_agent.cpp',
  'agentlib/agent_animation.cpp',
  'agentlib/llm_client.cpp',
  'agentlib/tool_registry.cpp',
  'agentlib/tool_validator.cpp',
  'agentlib/httplib_transport.cpp',
  'agentlib/recording_transport.cpp',
  'agentlib/replay_transport.cpp',
  'agentlib/file_security_manager.cpp',
  'agentlib/virtual_file_system.cpp',
  'agentlib/interactions/action.cpp',
  'agentlib/interactions/image_tool.cpp',
  'agentlib/interactions/base.cpp',
  'agentlib/interactions/user_message.cpp',
  'agentlib/interactions/llm_response.cpp',
  'agentlib/interactions/reasoning.cpp',
  'agentlib/interactions/terminal.cpp',
  'agentlib/interactions/tool_interaction.cpp',
  'agentlib/interactions/system_message.cpp',
  'agentlib/ai_model.cpp',
  'agentlib/model_server.cpp',
  'agentlib/protocols/connection_factory.cpp',
  'agentlib/protocols/openai_completion_connection.cpp',
  'agentlib/protocols/openai_response_connection.cpp',
  'agentlib/protocols/gemini_connection.cpp',
  'agentlib/protocols/claude_connection.cpp',
  'agentlib/skill_manager.cpp',
  'agentlib/subagent_manager.cpp',
  'agentlib/compaction_engine.cpp',
  'agentlib/context_dnn.cpp',
  'agentlib/copilot_manager.cpp',
  'agentlib/data/turn.cpp',
  'agentlib/data/system_turn.cpp',
  'agentlib/data/user_turn.cpp',
  'agentlib/data/model_response_turn.cpp',
  'agentlib/data/tool_execution_turn.cpp',
  'agentlib/data/error_turn.cpp',
  'agentlib/data/transaction.cpp',
  'agentlib/data/episode.cpp',
  'agentlib/data/conversation.cpp',
  'agentlib/data/turn_registry.cpp',
  'mcp/mcp_server.cpp',
  'mcp/mcp_manager.cpp'
)

ui_sources = files(
  'ui/menu_bar.cpp',
  'ui/status_bar.cpp',
  'editor.cpp',
  'editor_events.cpp',
  'editor_events_mouse.cpp',
  'editor_events_ui.cpp',
  'editor_events_file.cpp',
  'editor_events_window.cpp',
  'editor_events_search.cpp',
  'editor_events_git.cpp',
  'editor_events_build.cpp',
  'editor_events_lsp.cpp',
  'editor_events_key.cpp',
  'ui/window.cpp',
  'ui/hex_editor_window.cpp',
  'ui/agent_window.cpp',
  'ui/agent_center_window.cpp',
  'ui/diff_window.cpp',
  'ui/crashdump_window.cpp',
  'ui/code_review_window.cpp',
  'ui/ansi_terminal_emulator.cpp',
  'ui/terminal_window.cpp',
  'ui/ui_element.cpp',
  'ui/components/ui_textbox.cpp',
  'ui/components/ui_multiline_edit.cpp',
  'ui/components/ui_listbox.cpp',
  'ui/components/ui_button.cpp',
  'ui/components/ui_buttons_horizontal.cpp',
  'ui/components/ui_buttons_vertical.cpp',
  'ui/components/ui_horizontal_flow.cpp',
  'ui/components/ui_vertical_flow.cpp',
  'ui/components/ui_grid_flow.cpp',
  'ui/components/ui_durmovie.cpp',
  'ui/components/ui_agent_tile.cpp',
  'ui/components/ui_checkbox.cpp',
  'ui/components/ui_checkbox_group.cpp',
  'ui/components/ui_color_picker.cpp',
  'ui/components/ui_radio.cpp',
  'ui/components/ui_ask_user_group.cpp',
  'ui/components/ui_group_box.cpp',
  'ui/components/ui_fileselector.cpp',
  'ui/components/ui_text_label.cpp',
  'ui/components/ui_dropdown.cpp',
  'ui/dialog.cpp',
  'ui/dialog_factories.cpp',
  'ui/popup_menu.cpp',
  'ui/components/ui_thumbnail.cpp',
  'ui/dynamic_colors.cpp'
)

# Helper for embedding help text
embed_prog = find_program('python3')
help_text_h = custom_target('help_text.h',
  input : '../docs/keybindings.md',
  output : 'help_text.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'help_text_md']
)

default_movie_h = custom_target('default_movie.h',
  input : '../artwork/default.json',
  output : 'default_movie.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'default_movie_json']
)

fs_compile_project_movie_h = custom_target('fs_compile_project_movie.h',
  input : '../artwork/fs_compile_project.json',
  output : 'fs_compile_project_movie.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'fs_compile_project_movie_json']
)

run_shell_command_movie_h = custom_target('run_shell_command_movie.h',
  input : '../artwork/run_shell_command.json',
  output : 'run_shell_command_movie.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'run_shell_command_movie_json']
)

run_python_movie_h = custom_target('run_python_movie.h',
  input : '../artwork/run_python.json',
  output : 'run_python_movie.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'run_python_movie_json']
)

turbostar_movie_h = custom_target('turbostar_movie.h',
  input : '../artwork/turbostar.json',
  output : 'turbostar_movie.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'turbostar_movie_json']
)

# Define static libraries
libturbostar_core = static_library('turbostar_core',
  core_sources,
  include_directories : inc,
  cpp_args : ['-DPLUGIN_DIR="@0@"'.format(plugin_dir), '-DTURBOCATCH_DIR="@0@"'.format(turbocatch_dir)],
  dependencies : [nlohmann_json_dep, thread_dep, sqlite3_dep, re2_dep, lsp_dep, libunwind_dep, zydis_dep])

libagentlib = static_library('agentlib',
  agentlib_sources + [research_agent_h, self_agent_h],
  include_directories : inc,
  dependencies : [httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, curl_dep, openssl_dep, yaml_cpp_dep])

tools_deps = [nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep, zydis_dep, zlib_dep, magic_dep]
if graphicsmagick_dep.found()
  tools_deps += [graphicsmagick_dep]
endif

libtools = static_library('tools',
  tools_sources,
  include_directories : inc,
  dependencies : tools_deps)

libturbostar_ui = static_library('turbostar_ui',
  ui_sources + [help_text_h, default_movie_h, fs_compile_project_movie_h, run_shell_command_movie_h, run_python_movie_h, turbostar_movie_h],
  include_directories : inc,
  dependencies : [ncurses, thread_dep, nlohmann_json_dep, lsp_dep, magic_dep])

# Expose dependencies for easy linking
core_dep = declare_dependency(link_with : libturbostar_core, dependencies : [libunwind_dep, zydis_dep, ncurses])
agent_dep = declare_dependency(link_whole : libagentlib, dependencies : [curl_dep, openssl_dep, yaml_cpp_dep])
tools_dep = declare_dependency(link_whole : libtools, dependencies : tools_deps)
ui_dep = declare_dependency(link_whole : libturbostar_ui, dependencies : [magic_dep])

subdir('plugins')
