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
#include "git_manager.h"
#include "history_manager.h"
#include "lsp_manager.h"

namespace fs = std::filesystem;

bool editor::handle_q_block_key(int key)
{
	auto &logger = event_logger::get_instance();
	int c = std::tolower(key);

	if (c == 'f') {
		logger.log("Q-block: Find Text");
		editor_event ev;
		ev.type = event_type::find;
		global_queue_.push(ev);
		return true;
	} else if (c == 'a') {
		logger.log("Q-block: Replace Text");
		editor_event ev;
		ev.type = event_type::replace;
		global_queue_.push(ev);
		return true;
	} else if (c == 'h') {
		logger.log("Q-block: Undo History");
		auto doc = get_active_doc();
		if (doc && doc->get_undo_count() > 0) {
			new_diff_window();
		} else {
			logger.log("No undo history available.");
		}
		return true;
	}
	return false;
}

bool editor::handle_p_block_key(int key)
{
	auto &logger = event_logger::get_instance();
	int c = std::tolower(key);
	logger.log("P-block handling key: " + std::to_string(key));

	if (c == 'r') {
		logger.log("P-block: Reformat");
		editor_event ev;
		ev.type = event_type::inline_agent_request;
		ev.payload = "Reformat the code in the target range.";
		global_queue_.push(ev);
		return true;
	} else if (c == 'f') {
		logger.log("P-block: Fix Warnings");
		editor_event ev;
		ev.type = event_type::inline_agent_request;
		ev.payload = "Fix any flagged warnings or errors in the target range.";
		global_queue_.push(ev);
		return true;
	} else if (c == 'c') {
		logger.log("P-block: Add Comments");
		editor_event ev;
		ev.type = event_type::inline_agent_request;
		ev.payload = "Add descriptive comments to the code in the target range.";
		global_queue_.push(ev);
		return true;
	} else if (c == 'v') {
		logger.log("P-block: Review");
		editor_event ev;
		ev.type = event_type::inline_agent_request;
		ev.payload = "Provide a code review and flag potential issues as errors or warnings in the target range.";
		global_queue_.push(ev);
		return true;
	} else if (c == 't') {
		logger.log("P-block: Complete TODOs");
		editor_event ev;
		ev.type = event_type::inline_agent_request;
		ev.payload = "Complete the function framework and any TODOs in the target range, removing completed TODO comments.";
		global_queue_.push(ev);
		return true;
	} else if (c == 's') {
		logger.log("P-block: Spell Check");
		editor_event ev;
		ev.type = event_type::inline_agent_request;
		ev.payload = "Spell check the target range. For code, focus on comments and strings. For non-code, check everything.";
		global_queue_.push(ev);
		return true;
	} else if (c == 'u') {
		logger.log("P-block: Custom Prompt");
		is_inline_agent_prompt_ = true;
		inline_agent_input_buffer_ = "";
		return true;
	}
	return false;
}

void editor::dispatch(const editor_event &ev)
{
	switch (ev.type) {
		case event_type::mouse_click:
			dispatch_event_mouse(ev);
			break;
		case event_type::quit:
		case event_type::force_quit:
		case event_type::redraw:
		case event_type::about:
		case event_type::settings:
		case event_type::models_config:
		case event_type::agent_switch_model:
		case event_type::help:
		case event_type::open_agent:
		case event_type::open_subagent:
		case event_type::open_crashdump_viewer:
		case event_type::agent_response:
		case event_type::agent_tool_update:
		case event_type::agent_save_history:
		case event_type::apply_edits:
		case event_type::prompt_user:
		case event_type::approve_plan:
		case event_type::set_transient_status:
		case event_type::inline_agent_request:
		case event_type::run_program:
		case event_type::run_settings:
		case event_type::run_in_debugger:
			dispatch_event_ui(ev);
			break;
		case event_type::load:
		case event_type::save:
		case event_type::save_all:
		case event_type::save_as:
		case event_type::new_doc:
		case event_type::format_doc:
		case event_type::open_file:
			dispatch_event_file(ev);
			break;
		case event_type::close_window:
		case event_type::select_window:
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
			dispatch_event_lsp(ev);
			break;
		case event_type::key_press:
			dispatch_event_key(ev);
			break;
		case event_type::paste: {
			if (active_dialog_mode_ != dialog_mode::none && active_dialog_) {
				active_dialog_->handle_event(ev, active_dialog_->x(), active_dialog_->y());
			} else {
				window *active_win = get_active_window();
				if (active_win) {
					active_win->get_queue().push(ev);
				}
			}
		} break;
		default:
			event_logger::get_instance().log(std::format("Unhandled event type dispatched: {}", static_cast<int>(ev.type)));
			break;
	}
}
