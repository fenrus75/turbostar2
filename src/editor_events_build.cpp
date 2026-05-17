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

namespace fs = std::filesystem;

void editor::dispatch_event_build(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::compile) {
		logger.log("Dispatching compile event.");
	
		// If we don't have a build process or it's not running
		if (!current_build_process_ || !current_build_process_->is_running()) {
			// Find or create the compile output window
			size_t compile_win_idx = static_cast<size_t>(-1);
			for (size_t i = 0; i < windows_.size(); ++i) {
				if (windows_[i]->get_title() == "Compile Output") {
					compile_win_idx = i;
					break;
				}
			}
	
			if (compile_win_idx == static_cast<size_t>(-1)) {
				// Make the main windows smaller if there's only one, or just overlay
				int compile_height = 10;
				auto doc = std::make_shared<document>(global_queue_, "Compile Output");
				documents_.push_back(doc);
	
				auto win = std::make_unique<window>(static_cast<int>(windows_.size() + 1), 0, LINES - compile_height - 1, COLS, compile_height, "Compile Output");
				win->attach_document(doc);
				win->set_display_priority(10); // Put it above normal windows
				win->set_background_color_pair(29); // White on Black
	
				windows_.push_back(std::move(win));				compile_win_idx = windows_.size() - 1;
			}
	
			activate_window(compile_win_idx);
			window* compile_win = windows_[compile_win_idx].get();
			compile_win->set_visible(true);
			current_build_process_ = std::make_unique<process_runner>(compile_win->get_document(), 1000);
			current_build_process_->set_parser(std::make_unique<gcc_log_parser>());
	
			std::string build_system = config_manager::get_instance().get_build_system();			std::string build_dir = config_manager::get_instance().get_build_directory();
			std::string cmd;
	
			if (build_system == "meson") {
				cmd = "meson compile -C " + build_dir;
			} else if (build_system == "cmake") {
				cmd = "cmake --build " + build_dir;
			} else if (build_system == "make") {
				cmd = "make -C " + build_dir;
			} else {
				cmd = build_system + " " + build_dir; // Fallback
			}
	
			current_build_process_->execute(cmd);
		} else {
			logger.log("Build already running.");
		}
	
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return;
	}

	if (ev.type == event_type::compile_file) {
		logger.log("Dispatching compile_file event.");
		
		std::shared_ptr<document> active_doc = get_active_doc();
		if (!active_doc || active_doc->get_filename().empty()) {
			logger.log("No active file to compile.");
			return;
		}
	
		if (!current_build_process_ || !current_build_process_->is_running()) {
			std::string build_dir = config_manager::get_instance().get_build_directory();
			std::string cmd = fs_utils::get_compile_command_for_file(active_doc->get_filename(), build_dir);
			if (cmd.empty()) {
				logger.log("Could not find compile command for file: " + active_doc->get_filename());
				// Fallback to normal compile? Or just do nothing.
				return;
			}
	
			size_t compile_win_idx = static_cast<size_t>(-1);
			for (size_t i = 0; i < windows_.size(); ++i) {
				if (windows_[i]->get_title() == "Compile Output") {
					compile_win_idx = i;
					break;
				}
			}
	
			if (compile_win_idx == static_cast<size_t>(-1)) {
				int compile_height = 10;
				auto doc = std::make_shared<document>(global_queue_, "Compile Output");
				documents_.push_back(doc);
				
				auto win = std::make_unique<window>(static_cast<int>(windows_.size() + 1), 0, LINES - compile_height - 1, COLS, compile_height, "Compile Output");
				win->attach_document(doc);
				win->set_display_priority(10);
				win->set_background_color_pair(29);
				
				windows_.push_back(std::move(win));
				compile_win_idx = windows_.size() - 1;
			}
	
			activate_window(compile_win_idx);
			window* compile_win = windows_[compile_win_idx].get();
			compile_win->set_visible(true);
			current_build_process_ = std::make_unique<process_runner>(compile_win->get_document(), 1000);
			current_build_process_->set_parser(std::make_unique<gcc_log_parser>());
			
			current_build_process_->execute(cmd);
		} else {
			logger.log("Build already running.");
		}
		
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return;
	}

	if (ev.type == event_type::run_tests) {
		logger.log("Dispatching run_tests event.");
	
		if (!current_build_process_ || !current_build_process_->is_running()) {
			size_t test_win_idx = static_cast<size_t>(-1);
			for (size_t i = 0; i < windows_.size(); ++i) {
				if (windows_[i]->get_title() == "Test Output") {
					test_win_idx = i;
					break;
				}
			}
	
			if (test_win_idx == static_cast<size_t>(-1)) {
				int test_height = 10;
				auto doc = std::make_shared<document>(global_queue_, "Test Output");
				documents_.push_back(doc);
	
				auto win = std::make_unique<window>(static_cast<int>(windows_.size() + 1), 0, LINES - test_height - 1, COLS, test_height, "Test Output");
				win->attach_document(doc);
				win->set_display_priority(10);
				win->set_background_color_pair(29); // White on Black
	
				windows_.push_back(std::move(win));				test_win_idx = windows_.size() - 1;
			}
	
			activate_window(test_win_idx);
			window* test_win = windows_[test_win_idx].get();
			test_win->set_visible(true);
			current_build_process_ = std::make_unique<process_runner>(test_win->get_document(), 1000);
			current_build_process_->set_parser(std::make_unique<gcc_log_parser>());
	
			std::string build_system = config_manager::get_instance().get_build_system();			std::string build_dir = config_manager::get_instance().get_build_directory();
			std::string cmd;
	
			if (build_system == "meson") {
				cmd = "meson test -C " + build_dir;
			} else if (build_system == "cmake") {
				cmd = "ctest --test-dir " + build_dir;
			} else if (build_system == "make") {
				cmd = "make test -C " + build_dir;
			} else {
				cmd = build_system + " test " + build_dir; // Fallback
			}
	
			current_build_process_->execute(cmd);
		} else {
			logger.log("Process already running.");
		}
	
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return;
	}

	if (ev.type == event_type::next_error) {
		logger.log("Dispatching next_error event.");
		auto err_opt = build_error_manager::get_instance().get_next_error();
		if (err_opt) {
			const auto& err = *err_opt;
			bool auto_open = config_manager::get_instance().is_auto_open_error_files();
	
			// 1. Find or open window for this file
			size_t win_idx = static_cast<size_t>(-1);
			std::string err_abs = fs_utils::safe_absolute(err.filepath).string();
			for (size_t i = 0; i < windows_.size(); ++i) {
				if (windows_[i]->get_document()) {
					std::string win_file = windows_[i]->get_document()->get_filename();
					if (win_file == err.filepath || fs_utils::safe_absolute(win_file).string() == err_abs) {
						win_idx = i;
						break;
					}
				}
			}
	
			if (win_idx == static_cast<size_t>(-1) && auto_open) {
				new_window(err.filepath);
				win_idx = windows_.size() - 1;
			}
	
			if (win_idx != static_cast<size_t>(-1)) {
				activate_window(win_idx);
				auto doc = windows_[win_idx]->get_document();
				if (doc) {
					// Jump to error
					doc->move_to_top();
					doc->move_cursor(err.column, err.line);
				}
	
				// 2. Auto-scroll compile window to the raw error text
				for (auto& w : windows_) {
					if (w->get_title() == "Compile Output" || w->get_title() == "Test Output") {
						if (current_build_process_) {
							current_build_process_->set_auto_scroll(false);
						}
						auto out_doc = w->get_document();
						if (out_doc) {
							out_doc->move_to_top();
							out_doc->move_cursor(0, err.output_buffer_line);
						}
					}
				}
			}
		}
	
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return;
	}

}
