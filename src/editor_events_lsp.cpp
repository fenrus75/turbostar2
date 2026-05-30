#include <algorithm>
#include <chrono>
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
			int total_lines = static_cast<int>(active_doc->line_count());

			// Always find and update the "optimal" enclosing scope (largest not whole file)
			const text_range *enclosing = nullptr;
			for (const auto &range : ev.highlight_ranges) {
				int range_height = range.end_y - range.start_y;
				// If range is almost the whole file (say > 95%), we stop at the previous one
				if (range_height >= total_lines - 2 && enclosing != nullptr) {
					break;
				}
				enclosing = &range;
			}
			if (enclosing) {
				active_doc->set_enclosing_scope(*enclosing);
			}

			if (pending_inline_agent_task_.active) {
				// Special logic for headless agent: Use the enclosing scope we just found
				if (enclosing) {
					active_doc->set_selection(enclosing->start_y, enclosing->start_x, enclosing->end_y,
								  enclosing->end_x);
				}

				launch_inline_agent(pending_inline_agent_task_.prompt);
				pending_inline_agent_task_.active = false;
				active_doc->clear_selection(); // Clear it after launch so user doesn't see it lingering
			} else {
				// Standard expansion logic: Innermost strictly larger than current
				int sel_start_x = -1, sel_start_y = -1, sel_end_x = -1, sel_end_y = -1;
				bool has_sel = active_doc->has_selection();
				if (has_sel) {
					active_doc->get_selection_range(sel_start_x, sel_start_y, sel_end_x, sel_end_y);
				}

				for (const auto &range : ev.highlight_ranges) {
					if (!has_sel) {
						active_doc->set_selection(range.start_y, range.start_x, range.end_y, range.end_x);
						break;
					} else {
						if (range.start_y < sel_start_y ||
						    (range.start_y == sel_start_y && range.start_x < sel_start_x) ||
						    range.end_y > sel_end_y || (range.end_y == sel_end_y && range.end_x > sel_end_x)) {

							active_doc->set_selection(range.start_y, range.start_x, range.end_y, range.end_x);
							break;
						}
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
