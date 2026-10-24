/**
 * @file editor_events.cpp
 * @brief Central event dispatching logic for the Turbostar editor.
 *
 * NOTE: Due to its size, the event handling implementation has been split into logical sub-modules
 * based on the `event_type` enum. If you are looking for specific event logic, please check
 * the corresponding file:
 *
 * - `editor_events_build.cpp`: Compilation and test running (event_type::compile, run_tests, etc.)
 * - `editor_events_file.cpp`: File I/O operations (event_type::load, save, save_as, save_all)
 * - `editor_events_git.cpp`: Git integration (event_type::git_add, git_refresh)
 * - `editor_events_key.cpp`: Raw keyboard input (event_type::key_press)
 * - `editor_events_lsp.cpp`: Language Server Protocol interactions (Expand Selection, etc.)
 * - `editor_events_mouse.cpp`: Mouse clicks and dragging (event_type::mouse_click, etc.)
 * - `editor_events_search.cpp`: Find and Replace dialogs (event_type::find, replace)
 * - `editor_events_ui.cpp`: Global UI events (event_type::quit, force_quit, settings, redraw)
 * - `editor_events_window.cpp`: Window management (event_type::close_window, next_window)
 */

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <lsp/json/json.h>
#include <ncurses.h>
#include <sstream>
#include "build_error_manager.h"
#include "config_manager.h"
#include "editor.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "gcc_log_parser.h"
#include "history_manager.h"
#include "lsp_manager.h"

namespace fs = std::filesystem;

bool editor::handle_q_block_key(int key)
{
	auto &logger = event_logger::get_instance();
	int c = std::tolower(key);

	switch (c) {
		case 'f': {
			logger.log("Q-block: Find Text");
			global_queue_.push(event_type::find);
			return true;
		}
		case 'a': {
			logger.log("Q-block: Replace Text");
			global_queue_.push(event_type::replace);
			return true;
		}
		case 'h': {
			logger.log("Q-block: Undo History");
			auto doc = get_active_doc();
			if (doc && doc->get_undo_count() > 0) {
				new_diff_window();
			} else {
				logger.log("No undo history available.");
			}
			return true;
		}
		case 's':
		case 'w': {
			logger.log("Q-block: Save As");
			global_queue_.push(event_type::save_as);
			return true;
		}
		default:
			break;
	}
	return false;
}

bool editor::handle_p_block_key(int key)
{
	auto &logger = event_logger::get_instance();
	int c = std::tolower(key);
	logger.log(std::format("P-block handling key: {}", key));

	switch (c) {
		case 'r': {
			logger.log("P-block: Reformat");
			global_queue_.push(event_type::inline_agent_request, "Reformat the code in the target range.");
			return true;
		}
		case 'f': {
			logger.log("P-block: Fix Warnings");
			global_queue_.push(event_type::inline_agent_request, "Fix any flagged warnings or errors in the target range.");
			return true;
		}
		case 'c': {
			logger.log("P-block: Add Comments");
			global_queue_.push(event_type::inline_agent_request, "Add descriptive comments to the code in the target range.");
			return true;
		}
		case 'v': {
			logger.log("P-block: Review");
			global_queue_.push(event_type::inline_agent_request,
					   "Provide a code review and flag potential issues as errors or warnings in the target range.");
			return true;
		}
		case 't': {
			logger.log("P-block: Complete TODOs");
			global_queue_.push(
			    event_type::inline_agent_request,
			    "Complete the function framework and any TODOs in the target range, removing completed TODO comments.");
			return true;
		}
		case 's': {
			logger.log("P-block: Spell Check");
			global_queue_.push(
			    event_type::inline_agent_request,
			    "Spell check the target range. For code, focus on comments and strings. For non-code, check everything.");
			return true;
		}
		case 'u': {
			logger.log("P-block: Custom Prompt");
			active_mode_ = input_mode::inline_agent;
			inline_agent_input_buffer_ = "";
			return true;
		}
		default:
			break;
	}
	return false;
}

void editor::dispatch(const editor_event &ev)
{
	if (ev.type == event_type::redraw) {
		needs_full_redraw_ = true;
	}
	switch (ev.type) {
		case event_type::run_on_main: {
			// Execute a packaged task handed over from a background thread and let it
			// fulfill its promise so the caller unblocks. Handled directly here because
			// it must run on the main/UI thread (not dispatched to the generic UI
			// handler, which may be entered from other contexts).
			auto task = std::static_pointer_cast<std::packaged_task<void()>>(ev.generic_promise);
			if (task) {
				(*task)();
			}
			break;
		}
		case event_type::mouse_click:
		case event_type::mouse_right_click:
		case event_type::mouse_double_click:
		case event_type::mouse_triple_click:
		case event_type::mouse_scroll_up:
		case event_type::mouse_scroll_down:
		case event_type::mouse_release:
		case event_type::mouse_drag:
			dispatch_event_mouse(ev);
			break;
		case event_type::quit:
		case event_type::force_quit:
		case event_type::redraw:
		case event_type::notify_undo_changed:
		case event_type::about:
		case event_type::new_project:
		case event_type::plugins:
		case event_type::settings:
		case event_type::editor_settings_config:
		case event_type::ai_settings_config:
		case event_type::a2a_settings_config:
		case event_type::syntax_colors_config:
		case event_type::models_config:
		case event_type::model_servers_config:
		case event_type::mcp_config:
		case event_type::a2a_config:
		case event_type::copilot_connect:
		case event_type::agent_switch_model:
		case event_type::help:
		case event_type::tool_status:
		case event_type::open_agent:
		case event_type::open_agent_center:
		case event_type::open_subagent:
		case event_type::rescan_subagents:
		case event_type::open_crashdump_viewer:
		case event_type::agent_response:
		case event_type::agent_tool_update:
		case event_type::agent_save_history:
		case event_type::apply_edits:
		case event_type::prompt_user:
		case event_type::set_transient_status:
		case event_type::inline_agent_request:
		case event_type::run_program:
		case event_type::run_settings:
		case event_type::run_in_debugger:
		case event_type::run_profile:
		case event_type::go_to_next_hotspot:
		case event_type::terminate_run:
		case event_type::agent_start_app:
		case event_type::agent_start_coredump_gdb:
		case event_type::task_models_config:
		case event_type::open_codereview_viewer:
		case event_type::codereview_action:
		case event_type::codereview_updated:
		case event_type::image_manager:
		case event_type::open_terminal:
			dispatch_event_ui(ev);
			break;
		case event_type::load:
		case event_type::save:
		case event_type::save_all:
		case event_type::save_as:
		case event_type::write_block:
		case event_type::new_doc:
		case event_type::format_doc:
		case event_type::open_file:
		case event_type::open_prompt_editor:
		case event_type::prompt_insert_file:
			dispatch_event_file(ev);
			break;
		case event_type::close_window:
		case event_type::maximize_window:
		case event_type::select_window:
		case event_type::toggle_gutter_numbers:
		case event_type::agent_hide_sidebar:
		case event_type::agent_show_sidebar:
			dispatch_event_window(ev);
			break;
		case event_type::find:
		case event_type::replace:
			dispatch_event_search(ev);
			break;
		case event_type::git_status_updated:
		case event_type::git_add:
		case event_type::git_refresh:
			dispatch_event_git(ev);
			break;
		case event_type::compile:
		case event_type::compile_file:
		case event_type::run_tests:
		case event_type::next_error:
			dispatch_event_build(ev);
			break;
		case event_type::lsp_hover_result:
		case event_type::lsp_highlight_result:
		case event_type::lsp_selection_range_result:
		case event_type::lsp_diagnostics_result:
		case event_type::lsp_signature_result:
			dispatch_event_lsp(ev);
			break;
		case event_type::key_press:
			dispatch_event_key(ev);
			break;
		case event_type::paste: {
			if (active_dialog_mode_ != dialog_mode::none && active_dialog_) {
				active_dialog_->handle_event(ev, active_dialog_->x(), active_dialog_->y());
			} else if (active_mode_ == input_mode::searching) {
				search_input_buffer_ += ev.payload;
				global_queue_.push(event_type::redraw);
			} else if (active_mode_ == input_mode::search_options) {
				search_options_buffer_ += ev.payload;
				global_queue_.push(event_type::redraw);
			} else if (active_mode_ == input_mode::replace_query) {
				replace_input_buffer_ += ev.payload;
				global_queue_.push(event_type::redraw);
			} else if (active_mode_ == input_mode::going_to_line) {
				for (char c : ev.payload) {
					if (c >= '0' && c <= '9') {
						line_input_buffer_ += c;
					}
				}
				global_queue_.push(event_type::redraw);
			} else if (active_mode_ == input_mode::inline_agent) {
				inline_agent_input_buffer_ += ev.payload;
				global_queue_.push(event_type::redraw);
			} else if (active_mode_ == input_mode::vim) {
				vim_input_buffer_ += ev.payload;
				global_queue_.push(event_type::redraw);
			} else {
				window *active_win = get_active_window();
				if (active_win) {
					active_win->get_queue().push(ev);
				}
			}
		} break;
		default:
			event_logger::get_instance().log("Unhandled event type dispatched: {}", static_cast<int>(ev.type));
			break;
	}
}
