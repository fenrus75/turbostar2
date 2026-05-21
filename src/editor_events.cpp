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

#include "editor.h"
#include <algorithm>
#include <chrono>
#include <ncurses.h>
#include "event_logger.h"
#include "history_manager.h"
#include "config_manager.h"
#include "git_manager.h"
#include "lsp_manager.h"
#include "gcc_log_parser.h"
#include "build_error_manager.h"
#include "fs_utils.h"
#include <fstream>
#include <sstream>
#include <lsp/json/json.h>

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
		case event_type::help:
		case event_type::open_agent:
		case event_type::agent_response:
		case event_type::agent_tool_update:
		case event_type::apply_edits:
		case event_type::prompt_user:
			dispatch_event_ui(ev);
			break;
		case event_type::load:
		case event_type::save:
		case event_type::save_all:
		case event_type::save_as:
		case event_type::new_doc:
		case event_type::format_doc:
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
		default:
			break;
	}
}

