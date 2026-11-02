# AI & Agent Settings Dialogs (`dialog_ai`)

## Goals
The `dialog_ai` module implements Turbo Pascal style modal dialogs for configuring AI models, LLM provider servers, task-specific model mappings, GitHub Copilot authentication, and MCP tool servers:
1. `create_ai_settings_dialog`: Multi-tabbed configuration dialog ("General AI", "Security", "Model Providers", "Task Models", "MCP & Plugins").
2. `create_model_list_dialog`, `create_model_selection_dialog`, `create_model_edit_dialog`: CRUD and selection interfaces for `agentlib::ai_model_registry`.
3. `create_model_server_list_dialog`, `create_model_server_edit_dialog`: CRUD interfaces for LLM provider endpoints (`agentlib::model_server_registry`).
4. `create_mcp_config_dialog`, `create_mcp_tools_dialog`: Configuration of Model Context Protocol servers and individual tool enablement.
5. `create_copilot_connect_dialog`, `create_tool_status_dialog`: Device-flow authentication and external tool binary verification.

## Architecture and Constraints
- **Tabbed Layout & Focus Restoration**: Hosted in `ui_tabbed_container`. When returning to `create_ai_settings_dialog` from a child dialog (such as adding or editing a model provider), `active_tab_id` and `selected_server_id` restore the active tab page, highlight the target item in `server_list`, and place keyboard focus directly on the list.
- **Persistence Integration**: Model and provider edits persist immediately via `ai_model_registry::save_models()` and `model_server_registry::save_servers()`, while workspace/global AI settings are applied via `apply_ai_settings_from_dialog()`.

## Lessons Learned
- **Sub-Dialog Mode Transitions**: Launching a child dialog (like `create_model_server_edit_dialog`) from inside `create_ai_settings_dialog` (`dialog_mode::settings`) must transition `active_dialog_mode_` to `dialog_mode::model_server_edit` and record that it originated from AI settings (`model_server_edit_from_ai_settings_`). Leaving `active_dialog_mode_` as `dialog_mode::settings` causes confirming the child dialog (`"ok"`) to be misinterpreted as confirming the parent settings dialog, discarding the new server and closing all dialogs back to the editor window.
