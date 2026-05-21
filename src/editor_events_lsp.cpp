#include "editor.h"
#include <algorithm>
#include <chrono>
#include <ncurses.h>
#include "event_logger.h"
#include "ui/file_dialog.h"
#include "ui/find_dialog.h"
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

void editor::dispatch_event_lsp(const editor_event &ev)
{

	if (ev.type == event_type::lsp_hover_result) {
		std::string text = ev.payload;
		std::replace(text.begin(), text.end(), '\n', ' ');
		if (text.length() > static_cast<size_t>(COLS - 20)) {
			text = text.substr(0, COLS - 20) + "...";
		}
		hover_text_ = text;
		return;
	}

	if (ev.type == event_type::lsp_highlight_result) {
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc) {
			active_doc->set_lsp_highlights(ev.highlight_ranges);
		}
		return;
	}

	if (ev.type == event_type::lsp_selection_range_result) {
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc && !ev.highlight_ranges.empty()) {
			int sel_start_x = -1, sel_start_y = -1, sel_end_x = -1, sel_end_y = -1;
			bool has_sel = active_doc->has_selection();
			if (has_sel) {
				active_doc->get_selection_range(sel_start_x, sel_start_y, sel_end_x, sel_end_y);
			}
	
			// We traverse from innermost to outermost to find the first range strictly larger than the current selection.
			// Or if no selection exists, we pick the innermost one (the first element).
			for (const auto& range : ev.highlight_ranges) {
				if (!has_sel) {
					active_doc->set_selection(range.start_y, range.start_x, range.end_y, range.end_x);
					break;
				} else {
					// Check if this range is strictly larger than the current selection
					if (range.start_y < sel_start_y || 
					    (range.start_y == sel_start_y && range.start_x < sel_start_x) ||
					    range.end_y > sel_end_y ||
					    (range.end_y == sel_end_y && range.end_x > sel_end_x)) {
					    
					    active_doc->set_selection(range.start_y, range.start_x, range.end_y, range.end_x);
					    break;
					}
				}
			}
			
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
		}
		return;
	}

	if (ev.type == event_type::lsp_diagnostics_result) {
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc) {
			active_doc->set_lsp_diagnostics(ev.diagnostics);
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
		}
		return;
	}

}
