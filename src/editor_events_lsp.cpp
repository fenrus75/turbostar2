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
		set_status_message(text, status_priorities::HOVER);
		return;
	}

	if (ev.type == event_type::lsp_highlight_result) {
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc) {
			if (!ev.payload.empty()) {
				std::string safe_name = active_doc->get_safe_filename();
				std::string name = active_doc->get_filename();
				if (safe_name != ev.payload && name != ev.payload) {
					return;
				}
			}
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

			global_queue_.push(event_type::redraw);
		}
		return;
	}

	if (ev.type == event_type::lsp_diagnostics_result) {
		const std::string &target_file = ev.payload;
		if (target_file.empty()) {
			return;
		}

		std::filesystem::path target_p(target_file);
		bool matched_any = false;
		for (auto &doc : documents_) {
			if (!doc) {
				continue;
			}
			std::string doc_path = doc->get_filename();
			std::string safe_doc_path = doc->get_safe_filename();
			if (doc_path.empty() && safe_doc_path.empty()) {
				continue;
			}

			bool matches = false;
			if (!safe_doc_path.empty() && safe_doc_path == target_file) {
				matches = true;
			} else if (!doc_path.empty() && doc_path == target_file) {
				matches = true;
			} else {
				try {
					std::filesystem::path p(!safe_doc_path.empty() ? safe_doc_path : doc_path);
					if (std::filesystem::exists(p) && std::filesystem::exists(target_p)) {
						if (std::filesystem::equivalent(p, target_p)) {
							matches = true;
						}
					}
					if (!matches && std::filesystem::weakly_canonical(p).string() ==
							    std::filesystem::weakly_canonical(target_p).string()) {
						matches = true;
					}
				} catch (...) {
					// Ignore invalid paths
				}
			}

			if (matches) {
				doc->set_lsp_diagnostics(ev.diagnostics);
				matched_any = true;
			}
		}

		if (matched_any) {
			global_queue_.push(event_type::redraw);
		}
		return;
	}
}
