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

void editor::dispatch_event_file(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::load) {
		logger.log("Dispatching load event.");
		active_dialog_ = std::make_unique<file_dialog>("Open File", file_dialog_mode::open, true, ".");
		active_dialog_mode_ = dialog_mode::load;
		set_focus(focus_target::dialog, "menu_load");
		return;
	}

	if (ev.type == event_type::save) {
		logger.log("Dispatching save event (Smart Save).");
		std::shared_ptr<document> active_doc = get_active_doc();
	
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
			if (doc && doc->is_modified() && doc->has_nondefault_filename()) {
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
	
		std::string filename_arg;
		if (active_doc) {
			filename_arg = active_doc->get_filename();
		} else {
			filename_arg = ".";
		}
		active_dialog_ = std::make_unique<file_dialog>("Save File As", file_dialog_mode::save, false, filename_arg);
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

}
