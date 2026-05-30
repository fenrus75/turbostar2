subdir('tools')

core_sources = files(
  'line.cpp',
  'document.cpp',
  'document_nav.cpp',
  'document_undo.cpp',
  'document_selection.cpp',
  'document_edit.cpp',
  'document_search.cpp',
  'document_format.cpp',
  'document_highlight.cpp',
  'fs_utils.cpp',
  'config_manager.cpp',
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
  'cpp_highlighter.cpp',
  'python_highlighter.cpp',
  'markdown_highlighter.cpp',
  'highlighter_registry.cpp',
  'history_manager.cpp',
  'utf8.cpp'
)


agentlib_sources = files(
  'agentlib/ai_agent.cpp',
  'agentlib/llm_client.cpp',
  'agentlib/tool_registry.cpp',
  'agentlib/httplib_transport.cpp',
  'agentlib/recording_transport.cpp',
  'agentlib/replay_transport.cpp',
  'agentlib/file_security_manager.cpp',
  'agentlib/virtual_file_system.cpp',
  'agentlib/interactions/action.cpp',
  'agentlib/interactions/base.cpp',
  'agentlib/interactions/user_message.cpp',
  'agentlib/interactions/llm_response.cpp',
  'agentlib/interactions/reasoning.cpp',
  'agentlib/interactions/terminal.cpp',
  'agentlib/interactions/tool_interaction.cpp',
  'agentlib/interactions/system_message.cpp',
  'agentlib/ai_model.cpp',
  'agentlib/api_formatter.cpp',
  'agentlib/skill_manager.cpp',
  'agentlib/compaction_engine.cpp'
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
  'ui/agent_window.cpp',
  'ui/diff_window.cpp',
  'ui/agent_status_window.cpp',
  'ui/crashdump_window.cpp',
  'ui/ansi_terminal_emulator.cpp',
  'ui/terminal_window.cpp',
  'ui/ui_element.cpp',
  'ui/components/ui_textbox.cpp',
  'ui/components/ui_multiline_edit.cpp',
  'ui/components/ui_listbox.cpp',
  'ui/components/ui_button.cpp',
  'ui/components/ui_checkbox.cpp',
  'ui/components/ui_radio.cpp',
  'ui/components/ui_ask_user_group.cpp',
  'ui/components/ui_group_box.cpp',
  'ui/components/ui_fileselector.cpp',
  'ui/components/ui_text_label.cpp',
  'ui/components/ui_dropdown.cpp',
  'ui/dialog.cpp',
  'ui/dialog_factories.cpp',
  'ui/popup_menu.cpp'
)

# Helper for embedding help text
embed_prog = find_program('python3')
help_text_h = custom_target('help_text.h',
  input : '../docs/keybindings.md',
  output : 'help_text.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'help_text_md']
)

# Define static libraries
libturbostar_core = static_library('turbostar_core',
  core_sources,
  include_directories : inc,
  cpp_pch : 'pch/pch.h',
  dependencies : [nlohmann_json_dep, thread_dep, sqlite3_dep, re2_dep, lsp_dep])

libagentlib = static_library('agentlib',
  agentlib_sources,
  include_directories : inc,
  cpp_pch : 'pch/pch.h',
  dependencies : [httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep])

libtools = static_library('tools',
  tools_sources,
  include_directories : inc,
  cpp_pch : 'pch/pch.h',
  dependencies : [nlohmann_json_dep, sqlite3_dep, re2_dep, lsp_dep])

libturbostar_ui = static_library('turbostar_ui',
  ui_sources + [help_text_h],
  include_directories : inc,
  cpp_pch : 'pch/pch.h',
  dependencies : [ncurses, thread_dep, nlohmann_json_dep, lsp_dep])

# Expose dependencies for easy linking
core_dep = declare_dependency(link_with : libturbostar_core)
agent_dep = declare_dependency(link_whole : libagentlib)
tools_dep = declare_dependency(link_whole : libtools)
ui_dep = declare_dependency(link_whole : libturbostar_ui)
