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
#include "ui/dialog_factories.h"

namespace fs = std::filesystem;

void editor::dispatch_event_file(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::load) {
		logger.log("Dispatching load event.");
		active_dialog_ = create_file_dialog("Open File", ".");
		active_dialog_mode_ = dialog_mode::load;
		set_focus(focus_target::dialog, "menu_load");
		return;
	}

	if (ev.type == event_type::save) {
		logger.log("Dispatching save event (Smart Save).");
		std::shared_ptr<document> active_doc = get_active_doc();

		if (!active_doc || active_doc->is_read_only()) {
			logger.log("Cannot save read-only or empty buffer.");
			return;
		}

		if (active_doc && active_doc->has_nondefault_filename()) {
			active_doc->save();
			history_manager::get_instance().add_file(active_doc->get_filename());
			// Update window title and menu to clear dirty flag
			for (auto &w : windows_) {
				if (w->get_document() == active_doc) {
					w->set_title(active_doc->get_filename());
					break;
				}
			}
			update_window_menu();
			if (config_manager::get_instance().is_compile_on_save()) {
				editor_event compile_ev;
				compile_ev.type = event_type::compile_file;
				global_queue_.push(compile_ev);
			}
			return;
		}

		// Fallback to Save As logic
		editor_event save_as_ev;
		save_as_ev.type = event_type::save_as;
		dispatch(save_as_ev);
		return;
	}

	if (ev.type == event_type::save_all) {
		logger.log("Dispatching save_all event.");
		for (auto &doc : documents_) {
			if (doc && doc->is_modified() && doc->has_nondefault_filename() && !doc->is_read_only()) {
				doc->save();
				history_manager::get_instance().add_file(doc->get_filename());
				for (auto &w : windows_) {
					if (w->get_document() == doc) {
						w->set_title(doc->get_filename());
						break;
					}
				}
				if (config_manager::get_instance().is_compile_on_save()) {
					// We only compile the active document to avoid spawning too many processes.
					// Or we could compile the one that was just saved. But active is safer.
					if (doc == get_active_doc()) {
						editor_event compile_ev;
						compile_ev.type = event_type::compile_file;
						global_queue_.push(compile_ev);
					}
				}
			}
		}
		update_window_menu();
		return;
	}

	if (ev.type == event_type::save_as) {
		logger.log("Dispatching save_as event.");
		std::shared_ptr<document> active_doc = get_active_doc();

		if (!active_doc || active_doc->is_read_only()) {
			logger.log("Cannot save-as read-only or empty buffer.");
			return;
		}

		std::string filename_arg;
		if (active_doc) {
			filename_arg = active_doc->get_filename();
		} else {
			filename_arg = ".";
		}
		active_dialog_ = create_file_dialog("Save File As", filename_arg);
		active_dialog_mode_ = dialog_mode::save;
		set_focus(focus_target::dialog, "menu_save");
		return;
	}

	if (ev.type == event_type::new_doc) {
		logger.log("Dispatching new_doc event.");
		new_window("");
		return;
	}

	if (ev.type == event_type::revert) {
		logger.log("Dispatching revert event.");
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc) {
			if (active_doc->has_nondefault_filename()) {
				active_doc->load_from_file(active_doc->get_filename());
			} else {
				active_doc->clear_modified();
			}
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
		}
		return;
	}

	if (ev.type == event_type::format_doc) {
		logger.log("Dispatching format_doc event.");
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc) {
			active_doc->format_range(0, active_doc->line_count() - 1);
		}
		return;
	}

	if (ev.type == event_type::open_file) {
		logger.log("Dispatching open_file event for: " + ev.payload);
		if (ev.payload.empty())
			return;

		std::string filepath = ev.payload;
		int line_num = -1;
		size_t colon = filepath.find_last_of(':');
		if (colon != std::string::npos) {
			std::string suffix = filepath.substr(colon + 1);
			bool is_num = !suffix.empty() && std::all_of(suffix.begin(), suffix.end(), ::isdigit);
			if (is_num) {
				line_num = std::stoi(suffix);
				filepath = filepath.substr(0, colon);
			}
		}

		std::filesystem::path target_p(filepath);
		bool found = false;
		size_t activated_win_idx = static_cast<size_t>(-1);
		for (size_t i = 0; i < windows_.size(); ++i) {
			auto doc = windows_[i]->get_document();
			if (!doc)
				continue;
			std::string doc_path = doc->get_filename();
			if (doc_path.empty())
				continue;

			try {
				std::filesystem::path p(doc_path);
				if (std::filesystem::exists(p) && std::filesystem::exists(target_p)) {
					if (std::filesystem::equivalent(p, target_p)) {
						activate_window(i);
						set_focus(focus_target::window, "open_file");
						found = true;
						activated_win_idx = i;
						break;
					}
				}
				if (std::filesystem::weakly_canonical(p).string() == std::filesystem::weakly_canonical(target_p).string()) {
					activate_window(i);
					set_focus(focus_target::window, "open_file");
					found = true;
					activated_win_idx = i;
					break;
				}
			} catch (...) {
				// Ignore errors in path comparison
			}
		}

		if (!found) {
			new_window(filepath);
			set_focus(focus_target::window, "open_file");
			activated_win_idx = windows_.size() - 1;
		}

		if (activated_win_idx != static_cast<size_t>(-1) && line_num >= 1) {
			auto doc = windows_[activated_win_idx]->get_document();
			if (doc) {
				doc->move_to_top();
				doc->move_cursor(0, line_num - 1);
			}
		}

		// Force redraw to ensure the new/focused window renders immediately
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return;
	}
}
