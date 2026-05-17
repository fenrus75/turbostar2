#include "editor.h"
#include <algorithm>
#include <chrono>
#include <ncurses.h>
#include "event_logger.h"
#include "file_dialog.h"
#include "find_dialog.h"
#include "history_manager.h"
#include "config_manager.h"
#include "settings_dialog.h"
#include "git_manager.h"
#include "clangd_manager.h"
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

