#include <algorithm>
#include <chrono>
#include <ncurses.h>
#include "build_error_manager.h"
#include "config_manager.h"
#include "editor.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "gcc_log_parser.h"
#include "git_manager.h"
#include "history_manager.h"
#include "lsp_manager.h"
#include "ui/terminal_window.h"

namespace fs = std::filesystem;

void editor::dispatch_event_build(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::compile) {
		logger.log("Dispatching compile event.");
		save_all_documents();

		// Find or check if Compile Output terminal window is running
		size_t compile_win_idx = static_cast<size_t>(-1);
		for (size_t i = 0; i < windows_.size(); ++i) {
			if (windows_[i]->get_title() == "Compile Output") {
				compile_win_idx = i;
				break;
			}
		}

		bool is_running = false;
		if (compile_win_idx != static_cast<size_t>(-1)) {
			if (auto tw = dynamic_cast<ui::terminal_window *>(windows_[compile_win_idx].get())) {
				if (tw->is_alive()) {
					is_running = true;
				}
			}
		}

		if (!is_running) {
			if (compile_win_idx == static_cast<size_t>(-1)) {
				int compile_height = 10;
				auto win = std::make_unique<ui::terminal_window>(
					static_cast<int>(windows_.size() + 1), 0, LINES - compile_height - 1,
					COLS, compile_height, "Compile Output"
				);
				win->set_display_priority(10);	    // Put it above normal windows
				win->set_background_color_pair(29); // White on Black
				windows_.push_back(std::move(win));
				compile_win_idx = windows_.size() - 1;
			}

			activate_window(compile_win_idx);
			ui::terminal_window *compile_win = dynamic_cast<ui::terminal_window *>(windows_[compile_win_idx].get());
			compile_win->set_visible(true);

			std::string build_system = config_manager::get_instance().get_build_system();
			std::string build_dir = config_manager::get_instance().get_build_directory();
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

			build_error_manager::get_instance().clear();
			compile_win->set_capture_input(false); // Compilation output window doesn't capture user keys
			compile_win->start_process(cmd, std::make_unique<gcc_log_parser>());
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
		save_all_documents();

		std::shared_ptr<document> active_doc = get_active_doc();
		if (!active_doc || active_doc->get_filename().empty()) {
			logger.log("No active file to compile.");
			return;
		}

		std::string build_dir = config_manager::get_instance().get_build_directory();
		std::string cmd = fs_utils::get_compile_command_for_file(active_doc->get_filename(), build_dir);
		if (cmd.empty()) {
			logger.log("Could not find compile command for file: " + active_doc->get_filename());
			return;
		}

		size_t compile_win_idx = static_cast<size_t>(-1);
		for (size_t i = 0; i < windows_.size(); ++i) {
			if (windows_[i]->get_title() == "Compile Output") {
				compile_win_idx = i;
				break;
			}
		}

		bool is_running = false;
		if (compile_win_idx != static_cast<size_t>(-1)) {
			if (auto tw = dynamic_cast<ui::terminal_window *>(windows_[compile_win_idx].get())) {
				if (tw->is_alive()) {
					is_running = true;
				}
			}
		}

		if (!is_running) {
			if (compile_win_idx == static_cast<size_t>(-1)) {
				int compile_height = 10;
				auto win = std::make_unique<ui::terminal_window>(
					static_cast<int>(windows_.size() + 1), 0, LINES - compile_height - 1,
					COLS, compile_height, "Compile Output"
				);
				win->set_display_priority(10);
				win->set_background_color_pair(29);
				windows_.push_back(std::move(win));
				compile_win_idx = windows_.size() - 1;
			}

			activate_window(compile_win_idx);
			ui::terminal_window *compile_win = dynamic_cast<ui::terminal_window *>(windows_[compile_win_idx].get());
			compile_win->set_visible(true);

			build_error_manager::get_instance().clear();
			compile_win->set_capture_input(false); // Compilation output window doesn't capture user keys
			compile_win->start_process(cmd, std::make_unique<gcc_log_parser>());
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
		save_all_documents();

		size_t test_win_idx = static_cast<size_t>(-1);
		for (size_t i = 0; i < windows_.size(); ++i) {
			if (windows_[i]->get_title() == "Test Output") {
				test_win_idx = i;
				break;
			}
		}

		bool is_running = false;
		if (test_win_idx != static_cast<size_t>(-1)) {
			if (auto tw = dynamic_cast<ui::terminal_window *>(windows_[test_win_idx].get())) {
				if (tw->is_alive()) {
					is_running = true;
				}
			}
		}

		if (!is_running) {
			if (test_win_idx == static_cast<size_t>(-1)) {
				int test_height = 10;
				auto win = std::make_unique<ui::terminal_window>(
					static_cast<int>(windows_.size() + 1), 0, LINES - test_height - 1,
					COLS, test_height, "Test Output"
				);
				win->set_display_priority(10);
				win->set_background_color_pair(29);
				windows_.push_back(std::move(win));
				test_win_idx = windows_.size() - 1;
			}

			activate_window(test_win_idx);
			ui::terminal_window *test_win = dynamic_cast<ui::terminal_window *>(windows_[test_win_idx].get());
			test_win->set_visible(true);

			std::string build_system = config_manager::get_instance().get_build_system();
			std::string build_dir = config_manager::get_instance().get_build_directory();
			std::string cmd;

			if (build_system == "meson") {
				cmd = "meson test -C " + build_dir;
			} else if (build_system == "cmake") {
				cmd = "ctest --test-dir " + build_dir;
			} else if (build_system == "make") {
				cmd = "make test -C " + build_dir;
			} else {
				cmd = build_system + " test " + build_dir;
			}

			build_error_manager::get_instance().clear();
			test_win->set_capture_input(false); // Test output window doesn't capture user keys
			test_win->start_process(cmd, std::make_unique<gcc_log_parser>());
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
			const auto &err = *err_opt;
			bool auto_open = config_manager::get_instance().is_auto_open_error_files();

			// 1. Find or open window for this file
			size_t win_idx = static_cast<size_t>(-1);
			const std::string &err_abs = err.filepath;
			for (size_t i = 0; i < windows_.size(); ++i) {
				auto doc = windows_[i]->get_document();
				if (doc) {
					if (doc->get_safe_filename() == err_abs) {
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
				for (auto &w : windows_) {
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
