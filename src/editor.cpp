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

editor::editor(bool debug_mode, const std::string &debug_string, const std::vector<std::string> &filenames, bool exit_immediately, bool no_lsp)
    : exit_immediately_(exit_immediately), debug_mode_(debug_mode), debug_string_(debug_string)
{
	history_manager::get_instance().load();
	git_manager::get_instance().start(global_queue_);
	bool lsp_allowed = !no_lsp && config_manager::get_instance().is_lsp_enabled();
	if (lsp_allowed) {
		clangd_manager::get_instance().start(global_queue_);
	}

	if (filenames.empty()) {
		new_window("");
	} else {
		for (const auto &f : filenames) {
			new_window(f);
		}
	}
}

void editor::new_window(const std::string &filename)
{
	// Create document
	auto doc = std::make_shared<document>(global_queue_, filename);
	documents_.push_back(doc);

	// Create window
	auto win = std::make_unique<window>(static_cast<int>(windows_.size() + 1), 0, 1, COLS, LINES - 2, filename);
	win->attach_document(doc);

	windows_.push_back(std::move(win));
	activate_window(windows_.size() - 1);
}

void editor::activate_window(size_t index)
{
	if (index >= windows_.size())
		return;

	for (size_t i = 0; i < windows_.size(); ++i) {
		windows_[i]->set_active(i == index);
	}
	update_window_menu();
}

void editor::update_window_menu()
{
	std::vector<menu_item> items;
	
	items.push_back(menu_item("Close", event_type::close_window, 0, 'c', "Alt+F3", false));
	items.push_back(menu_item("", event_type::key_press, 0, 0, "", true)); // Separator

	for (size_t i = 0; i < windows_.size(); ++i) {
		std::string filename = windows_[i]->get_title();
		if (filename.empty())
			filename = "noname.txt";

		auto doc = windows_[i]->get_document();
		if (doc && doc->is_modified()) {
			filename += "*";
		}

		std::string name = std::to_string((i + 1) % 10) + " " + filename;

		std::string shortcut = "";
		char hotkey = 0;
		if (i < 10) {
			int num = static_cast<int>((i + 1) % 10);
			shortcut = "Alt-" + std::to_string(num);
			hotkey = static_cast<char>('0' + num);
		}

		items.push_back(menu_item(name, event_type::select_window, static_cast<int>(i), hotkey, shortcut, false));
	}
	event_logger::get_instance().log("update_window_menu: " + std::to_string(items.size()) + " items");
	top_menu_.set_category_items("Window", items);
}

std::shared_ptr<document> editor::get_active_doc() const
{
	for (auto &w : windows_) {
		if (w->is_active()) {
			return w->get_document();
		}
	}
	if (!documents_.empty()) {
		return documents_[0];
	}
	return nullptr;
}

window *editor::get_active_window() const
{
	for (auto &w : windows_) {
		if (w->is_active()) {
			return w.get();
		}
	}
	return nullptr;
}

std::string editor::get_search_autocomplete() const
{
	if (search_input_buffer_.empty())
		return "";

	const auto &history = history_manager::get_instance().get_searches();
	for (const auto &item : history) {
		if (item.length() >= search_input_buffer_.length() &&
		    item.substr(0, search_input_buffer_.length()) == search_input_buffer_) {
			return item;
		}
	}
	return "";
}

editor::~editor()
{
	clangd_manager::get_instance().stop();
	git_manager::get_instance().stop();
}

void editor::run()
{
	render();

	// Set ncurses getch timeout to 50ms to allow background events to
	// process
	timeout(50);
	auto start_time = std::chrono::steady_clock::now();

	while (is_running_) {
		if (exit_immediately_) {
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() > 1000) {
				is_running_ = false;
			}
		}

		wint_t wch;
		int res = get_wch(&wch);
		if (res != ERR) {
			editor_event ev;
			if (res == KEY_CODE_YES) {
				// Functional keys
				ev.type = event_type::key_press;
				ev.key_code = static_cast<int>(wch);
				global_queue_.push(ev);
			} else {
				// Character or ESC sequence
				if (wch == 27) { // ESC sequence
					timeout(0); // Non-blocking for sequence check
					wint_t next_wch;
					int next_res = get_wch(&next_wch);
					if (next_res != ERR && next_res != KEY_CODE_YES && next_wch == '[') {
						wint_t arrow_wch;
						int arrow_res = get_wch(&arrow_wch);
						if (arrow_res != ERR && arrow_res != KEY_CODE_YES) {
							int key = 0;
							switch (arrow_wch) {
								case 'A':
									key = KEY_UP;
									break;
								case 'B':
									key = KEY_DOWN;
									break;
								case 'C':
									key = KEY_RIGHT;
									break;
								case 'D':
									key = KEY_LEFT;
									break;
							}
							if (key != 0) {
								ev.type = event_type::key_press;
								ev.key_code = key;
								global_queue_.push(ev);
							}
						}
					} else if (next_res != ERR) {
						// Alt + key
						ev.type = event_type::key_press;
						ev.key_code = -static_cast<int>(next_wch);
						global_queue_.push(ev);
					} else {
						ev.type = event_type::key_press;
						ev.key_code = 27; // Bare ESC
						global_queue_.push(ev);
					}
					timeout(50); // Restore 50ms timeout
				} else {
					// Printable character or UTF-8 sequence
					ev.type = event_type::key_press;
					ev.key_code = static_cast<int>(wch);

					// Convert wide char to UTF-8 string
					char buf[8];
					int len = wctomb(buf, wch);
					if (len > 0) {
						ev.utf8_char.assign(buf, len);
					}

					global_queue_.push(ev);
				}
			}
		}

		bool needs_render = false;
		while (auto ev = global_queue_.pop()) {
			dispatch(*ev);
			needs_render = true;
		}

		for (auto &w : windows_) {
			if (w->process_events()) {
				needs_render = true;
			}
		}

		if (needs_render) {
			render();
		}
	}

	// Save history on exit
	history_manager::get_instance().save();
}

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

void editor::set_focus(focus_target target, const std::string &source)
{
	std::string target_name;
	switch (target) {
		case focus_target::menu_bar:
			target_name = "menu_bar";
			break;
		case focus_target::window:
			target_name = "window";
			break;
		case focus_target::dialog:
			target_name = "dialog";
			break;
	}

	event_logger::get_instance().log("Focus change: " + source + " -> " + target_name);
	current_focus_ = target;

	if (target == focus_target::menu_bar) {
		update_window_menu();
	}
}

bool editor::handle_k_block_key(int key)
{
	auto &logger = event_logger::get_instance();
	logger.log("K-block handling key: " + std::to_string(key));

	char c = 0;
	if (key > 0 && key <= 26) {
		c = static_cast<char>(key + 'a' - 1);
	} else if (key >= 0 && key < 256) {
		c = std::tolower(static_cast<char>(key));
	}

	if (c == 0)
		return false;

	// Find active window/doc
	std::shared_ptr<document> active_doc = get_active_doc();

	if (!active_doc)
		return false;

	if (c == 'b') {
		logger.log("K-block: Set Selection Begin");
		active_doc->set_selection_start();
		return true;
	} else if (c == 'k') {
		logger.log("K-block: Set Selection End");
		active_doc->set_selection_end();
		return true;
	} else if (c == 'h') {
		logger.log("K-block: Clear Selection");
		active_doc->clear_selection();
		return true;
	} else if (c == 'y') {
		logger.log("K-block: Delete Block");
		active_doc->delete_selection();
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return true;
	} else if (c == 'c') {
		logger.log("K-block: Copy Block");
		active_doc->copy_selection();
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return true;
	} else if (c == 'm') {
		logger.log("K-block: Move Block");
		active_doc->move_selection();
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return true;
	} else if (c == 'n') {
		logger.log("K-block: New Window");
		new_window("");
		return true;
	} else if (c == 'g') {
		logger.log("K-block: Next Error");
		editor_event err_ev;
		err_ev.type = event_type::next_error;
		global_queue_.push(err_ev);
		return true;
	} else if (c == 'e') {
		logger.log("K-block: Edit (Open File)");
		editor_event ev;
		ev.type = event_type::load;
		global_queue_.push(ev);
		return true;
	} else if (c == 'a') {
		logger.log("K-block: Save All");
		editor_event ev;
		ev.type = event_type::save_all;
		global_queue_.push(ev);
		return true;
	} else if (c == 'r') {
		logger.log("K-block: Insert File");
		active_dialog_ = std::make_unique<file_dialog>("Insert File", file_dialog_mode::open, false, ".");
		active_dialog_mode_ = dialog_mode::insert_file;
		set_focus(focus_target::dialog, "menu_insert_file");
		return true;
	} else if (c == 'j') {
		logger.log("K-block: Format Paragraph");
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc) {
			active_doc->format_paragraph();
		}
		return true;
	} else if (c == ']') {
		logger.log("K-block: Expand Selection (LSP)");
		if (active_doc && !active_doc->get_filename().empty()) {
			clangd_manager::get_instance().request_selection_range(
				active_doc->get_filename(),
				active_doc->get_cursor_y(),
				active_doc->get_cursor_x()
			);
		}
		return true;
	} else if (c == '[' || c == '{') {
		logger.log("K-block: Select Scope");
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc) {
			active_doc->select_enclosing_scope();
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
		}
		return true;
	} else if (c == 'd' || c == 's') {

		logger.log("K-block: Save File");
		editor_event ev;
		ev.type = event_type::save;
		global_queue_.push(ev);
		return true;
	} else if (c == 'w') {
		logger.log("K-block: Write (Save As)");
		editor_event ev;
		ev.type = event_type::save_as;
		global_queue_.push(ev);
		return true;
	} else if (c == 'q') {
		logger.log("K-block: Quit (Abort)");
		editor_event quit_ev;
		quit_ev.type = event_type::quit;
		global_queue_.push(quit_ev);
		return true;
	} else if (c == 'x') {
		logger.log("K-block: Save & Exit");
		active_doc->save();
		editor_event quit_ev;
		quit_ev.type = event_type::quit;
		global_queue_.push(quit_ev);
		return true;
	} else if (c == 'f') {
		logger.log("K-block: Find (Status Bar Prompt)");
		is_searching_prompt_ = true;
		search_input_buffer_ = "";
		return true;
	} else if (c == 'l') {
		logger.log("K-block: Go to Line (Status Bar Prompt)");
		is_going_to_line_prompt_ = true;
		line_input_buffer_ = "";
		return true;
	} else if (c == 'u') {
		logger.log("K-block: Top of File");
		active_doc->move_to_top();
		return true;
	} else if (c == 'v') {
		logger.log("K-block: End of File");
		active_doc->move_to_bottom();
		return true;
	}

	return false;
}

void editor::dispatch(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::quit) {
		logger.log("Dispatching quit event.");
		is_running_ = false;
		return;
	}

	if (ev.type == event_type::redraw) {
		logger.log("Dispatching redraw event.");
		return;
	}

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

	if (ev.type == event_type::close_window) {
		logger.log("Dispatching close_window event.");
		size_t active_idx = static_cast<size_t>(-1);
		for (size_t i = 0; i < windows_.size(); ++i) {
			if (windows_[i]->is_active()) {
				active_idx = i;
				break;
			}
		}

		if (active_idx != static_cast<size_t>(-1)) {
			// If it's a build process window, stop the process
			if (current_build_process_ && 
			    (windows_[active_idx]->get_title() == "Compile Output" || 
			     windows_[active_idx]->get_title() == "Test Output")) {
				current_build_process_->stop();
			}

			std::shared_ptr<document> doc = windows_[active_idx]->get_document();
			windows_.erase(windows_.begin() + active_idx);
			
			// Remove document if no other window is using it
			bool doc_used = false;
			for (const auto& w : windows_) {
				if (w->get_document() == doc) {
					doc_used = true;
					break;
				}
			}
			if (!doc_used) {
				for (auto it = documents_.begin(); it != documents_.end(); ++it) {
					if (*it == doc) {
						documents_.erase(it);
						break;
					}
				}
			}

			if (windows_.empty()) {
				new_window("");
			} else {
				size_t next_idx = (active_idx == 0) ? 0 : active_idx - 1;
				if (next_idx >= windows_.size()) next_idx = windows_.size() - 1;
				activate_window(next_idx);
			}
		}
		return;
	}

	if (ev.type == event_type::select_window) {
		logger.log("Selecting window: " + std::to_string(ev.key_code));
		activate_window(static_cast<size_t>(ev.key_code));
		return;
	}

	if (ev.type == event_type::about) {
		logger.log("Dispatching about event.");
		std::vector<std::string> about_lines = {"TurboStar Editor",   "Version 0.1.0",	 "", "A nostalgia inspired TUI editor", "",
							"Copyright (c) 2026", "Arjan van de Ven"};
		active_dialog_ = std::make_unique<message_dialog>("About TurboStar", about_lines);
		set_focus(focus_target::dialog, "menu_about");
		return;
	}

	if (ev.type == event_type::find) {
		logger.log("Dispatching find event (advanced dialog).");
		active_dialog_ = std::make_unique<find_dialog>("Find", current_search_, false);
		active_dialog_mode_ = dialog_mode::search;
		set_focus(focus_target::dialog, "menu_find");
		return;
	}

	if (ev.type == event_type::replace) {
		logger.log("Dispatching replace event (advanced dialog).");
		active_dialog_ = std::make_unique<find_dialog>("Replace", current_search_, true);
		active_dialog_mode_ = dialog_mode::replace;
		set_focus(focus_target::dialog, "menu_replace");
		return;
	}

	if (ev.type == event_type::settings) {
		logger.log("Dispatching settings event.");
		active_dialog_ = std::make_unique<settings_dialog>();
		active_dialog_mode_ = dialog_mode::settings;
		set_focus(focus_target::dialog, "menu_settings");
		return;
	}

	if (ev.type == event_type::git_status_updated) {
		logger.log("Dispatching git_status_updated event.");
		for (auto &doc : documents_) {
			git_info info = git_manager::get_instance().get_cached_info(doc->get_filename());
			doc->set_git_branch(info.branch);
		}
		for (auto &win : windows_) {
			win->invalidate();
		}
		return;
	}

	if (ev.type == event_type::git_add) {
		logger.log("Dispatching git_add event.");
		std::shared_ptr<document> doc = get_active_doc();
		if (doc && !doc->get_filename().empty()) {
			git_manager::get_instance().git_add(doc->get_filename());
		}
		return;
	}

	if (ev.type == event_type::git_refresh) {
		logger.log("Dispatching git_refresh event.");
		std::shared_ptr<document> doc = get_active_doc();
		if (doc && !doc->get_filename().empty()) {
			git_manager::get_instance().request_status(doc->get_filename());
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
			std::string err_abs = fs_utils::safe_absolute(err.filepath).lexically_normal().string();
			for (size_t i = 0; i < windows_.size(); ++i) {
				if (windows_[i]->get_document()) {
					std::string win_file = windows_[i]->get_document()->get_filename();
					if (win_file == err.filepath || fs_utils::safe_absolute(win_file).lexically_normal().string() == err_abs) {
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

	if (ev.type == event_type::key_press) {
		hover_text_ = ""; // Clear hover text on any input
		logger.log("Dispatching key_press event: " + std::to_string(ev.key_code));

		// Global shortcuts
		if (ev.key_code == KEY_F(1)) {
			logger.log("Help shortcut pressed.");
			return;
		}
		if (ev.key_code == KEY_F(2)) {
			logger.log("Save shortcut pressed.");
			editor_event save_ev;
			save_ev.type = event_type::save;
			global_queue_.push(save_ev);
			return;
		}
		if (ev.key_code == KEY_F(3)) {
			logger.log("Open shortcut pressed.");
			editor_event load_ev;
			load_ev.type = event_type::load;
			global_queue_.push(load_ev);
			return;
		}
		if (ev.key_code == KEY_F(4)) {
			editor_event err_ev;
			err_ev.type = event_type::next_error;
			global_queue_.push(err_ev);
			return;
		}
		if (ev.key_code == KEY_F(9)) {
			editor_event compile_ev;
			compile_ev.type = event_type::compile;
			global_queue_.push(compile_ev);
			return;
		}
		if (ev.key_code == KEY_F(8)) {
			editor_event test_ev;
			test_ev.type = event_type::run_tests;
			global_queue_.push(test_ev);
			return;
		}
		if (ev.key_code == -static_cast<int>(KEY_F(3))) {
			editor_event close_ev;
			close_ev.type = event_type::close_window;
			global_queue_.push(close_ev);
			return;
		}

		// 1. Modal Dialogs have highest priority
		if (current_focus_ == focus_target::dialog && active_dialog_) {
			dialog_result res = active_dialog_->handle_key(ev.key_code);
			if (res == dialog_result::confirmed) {
				auto doc = get_active_doc();
				if (!doc)
					doc = documents_[0]; // Default fallback

				if (active_dialog_mode_ == dialog_mode::load) {
					std::string result_path = active_dialog_->get_result();
					new_window(result_path);
					history_manager::get_instance().add_file(result_path);
				} else if (active_dialog_mode_ == dialog_mode::save) {
					std::string result_path = active_dialog_->get_result();
					doc->save_to_file(result_path);
					history_manager::get_instance().add_file(result_path);
					// Update window title
					for (auto &w : windows_) {
						if (w->get_document() == doc) {
							w->set_title(doc->get_filename());
							break;
						}
					}
					update_window_menu();
				} else if (active_dialog_mode_ == dialog_mode::search || active_dialog_mode_ == dialog_mode::replace) {
					auto f_dialog = dynamic_cast<find_dialog *>(active_dialog_.get());
					if (f_dialog) {
						current_search_ = f_dialog->get_search_params();
						history_manager::get_instance().add_search(current_search_.query);
						if (doc->find_next(current_search_)) {
							editor_event redraw_ev;
							redraw_ev.type = event_type::redraw;
							global_queue_.push(redraw_ev);
						}
					}
				} else if (active_dialog_mode_ == dialog_mode::insert_file) {
					std::string result_path = active_dialog_->get_result();
					if (doc) {
						doc->insert_file(result_path);
					}
				} else if (active_dialog_mode_ == dialog_mode::settings) {
					auto s_dialog = dynamic_cast<settings_dialog *>(active_dialog_.get());
					if (s_dialog) {
						config_manager::get_instance().set_clang_format_style(s_dialog->get_selected_style());
						config_manager::get_instance().set_build_system(s_dialog->get_build_system());
						config_manager::get_instance().set_build_directory(s_dialog->get_build_directory());
						config_manager::get_instance().set_lsp_enabled(s_dialog->is_lsp_enabled());
						config_manager::get_instance().set_auto_open_error_files(s_dialog->is_auto_open_error_files());
						config_manager::get_instance().save();
					}
				}
				active_dialog_.reset();
				active_dialog_mode_ = dialog_mode::none;
				set_focus(focus_target::window, "dialog_close");
			} else if (res == dialog_result::cancelled) {
				active_dialog_.reset();
				active_dialog_mode_ = dialog_mode::none;
				set_focus(focus_target::window, "dialog_cancel");
			}
			return;
		}

		// 2. Status Bar Search Prompt
		if (is_searching_prompt_) {
			if (ev.key_code == 27) { // ESC
				is_searching_prompt_ = false;
				return;
			}
			std::string suggestion = get_search_autocomplete();
			
			if (ev.key_code == 9) { // TAB
				if (!suggestion.empty()) {
					search_input_buffer_ = suggestion;
				}
				return;
			}

			if (ev.key_code == 13 || ev.key_code == 10 || ev.key_code == KEY_ENTER) {
				if (!suggestion.empty() && search_input_buffer_ != suggestion) {
					search_input_buffer_ = suggestion;
					return;
				}
				
				current_search_.query = search_input_buffer_;
				
				is_searching_prompt_ = false;
				is_search_options_prompt_ = true;
				search_options_buffer_ = "";
				return;
			}
			if (ev.key_code == KEY_BACKSPACE || ev.key_code == 127 || ev.key_code == 8) {
				if (!search_input_buffer_.empty())
					search_input_buffer_.pop_back();
				return;
			}
			if (!ev.utf8_char.empty() && ev.key_code >= 32) {
				search_input_buffer_ += ev.utf8_char;
				return;
			}
			return; // Consume all keys in prompt mode
		}

		// 2.5 Status Bar Search Options Prompt
		if (is_search_options_prompt_) {
			if (ev.key_code == 27) { // ESC
				is_search_options_prompt_ = false;
				return;
			}
			if (ev.key_code == 13 || ev.key_code == 10 || ev.key_code == KEY_ENTER) {
				// Parse options
				current_search_.backward = false;
				current_search_.selected_text_only = false;
				current_search_.ignore_case = false;
				current_search_.from_cursor = true;
				
				bool is_replace = false;
				for (char c : search_options_buffer_) {
					char lc = std::tolower(c);
					if (lc == 'b') current_search_.backward = true;
					if (lc == 'k') current_search_.selected_text_only = true;
					if (lc == 'i') current_search_.ignore_case = true;
					if (lc == 'r') is_replace = true;
				}
				
				history_manager::get_instance().add_search(current_search_.query);

				if (is_replace) {
					is_search_options_prompt_ = false;
					active_dialog_ = std::make_unique<find_dialog>("Replace", current_search_, true);
					active_dialog_mode_ = dialog_mode::replace;
					set_focus(focus_target::dialog, "menu_replace");
					return;
				}

				std::shared_ptr<document> active_doc = get_active_doc();

				if (active_doc && active_doc->find_next(current_search_)) {
					editor_event redraw_ev;
					redraw_ev.type = event_type::redraw;
					global_queue_.push(redraw_ev);
				}
				is_search_options_prompt_ = false;
				return;
			}
			if (ev.key_code == KEY_BACKSPACE || ev.key_code == 127 || ev.key_code == 8) {
				if (!search_options_buffer_.empty())
					search_options_buffer_.pop_back();
				return;
			}
			if (!ev.utf8_char.empty() && ev.key_code >= 32) {
				char c = std::tolower(ev.utf8_char[0]);
				if (c == 'i' || c == 'r' || c == 'b' || c == 'k') {
					if (search_options_buffer_.find(c) == std::string::npos && search_options_buffer_.find(std::toupper(c)) == std::string::npos) {
						search_options_buffer_ += static_cast<char>(std::toupper(c));
					}
				}
				return;
			}
			return; // Consume all keys in prompt mode
		}

		// 3. Status Bar Go to Line Prompt
		if (is_going_to_line_prompt_) {
			if (ev.key_code == 27) { // ESC
				is_going_to_line_prompt_ = false;
				return;
			}
			if (ev.key_code == 13 || ev.key_code == 10 || ev.key_code == KEY_ENTER) {
				try {
					if (!line_input_buffer_.empty()) {
						int line_num = std::stoi(line_input_buffer_);
						std::shared_ptr<document> active_doc = get_active_doc();
						if (active_doc) {
							// Move to line (convert 1-based to 0-based)
							active_doc->move_cursor(0, (line_num - 1) - active_doc->get_cursor_y());
						}
					}
				} catch (...) {
					// Ignore invalid input
				}
				is_going_to_line_prompt_ = false;
				return;
			}
			if (ev.key_code == KEY_BACKSPACE || ev.key_code == 127 || ev.key_code == 8) {
				if (!line_input_buffer_.empty())
					line_input_buffer_.pop_back();
				return;
			}
			if (ev.key_code >= '0' && ev.key_code <= '9') {
				line_input_buffer_ += static_cast<char>(ev.key_code);
				return;
			}
			return; // Consume all keys in prompt mode
		}

		if (ev.key_code == 12) { // Ctrl-L
			logger.log("Repeating last search.");
			std::shared_ptr<document> active_doc = get_active_doc();
			if (active_doc) {
				logger.log("Active doc found for ^L. query=" + current_search_.query +
					   " backward=" + std::to_string(current_search_.backward));
				bool found = active_doc->find_next(current_search_, true);
				logger.log("find_next returned: " + std::to_string(found));
				if (found) {
					editor_event redraw_ev;
					redraw_ev.type = event_type::redraw;
					global_queue_.push(redraw_ev);
				}
			}
			return;
		}

		if (k_block_mode_) {
			if (handle_k_block_key(ev.key_code)) {
				k_block_mode_ = false;
				return;
			}
			k_block_mode_ = false;
		}

		if (q_block_mode_) {
			if (handle_q_block_key(ev.key_code)) {
				q_block_mode_ = false;
				return;
			}
			q_block_mode_ = false;
		}

		if (ev.key_code == 11) { // Ctrl-K
			logger.log("Entering K-block mode.");
			k_block_mode_ = true;
			return;
		}

		if (ev.key_code == 17) { // Ctrl-Q
			logger.log("Entering Q-block mode.");
			q_block_mode_ = true;
			return;
		}

		if (ev.key_code == 31) { // Ctrl-_ (Undo)
			logger.log("Undo requested.");
			std::shared_ptr<document> active_doc = get_active_doc();
			if (active_doc) {
				active_doc->undo();
			}
			return;
		}

		if (ev.key_code == 30) { // Ctrl-^ (Redo)
			logger.log("Redo requested.");
			std::shared_ptr<document> active_doc = get_active_doc();
			if (active_doc) {
				active_doc->redo();
			}
			return;
		}

		if (ev.key_code < 0) { // Alt + key
			char c = static_cast<char>(-ev.key_code);
			if (c >= '0' && c <= '9') {
				int target_idx;
				if (c == '0') {
					target_idx = 9;
				} else {
					target_idx = (c - '1');
				}
				activate_window(static_cast<size_t>(target_idx));
				return;
			}
			if (top_menu_.handle_alt_key(c, global_queue_)) {
				set_focus(focus_target::menu_bar, "alt_key");
				return;
			}
		}

		// Route based on focus (Windows/Menu Bar)
		if (current_focus_ == focus_target::menu_bar) {
			if (top_menu_.handle_key(ev.key_code, global_queue_)) {
				if (!top_menu_.is_open()) {
					set_focus(focus_target::window, "menu_close");
				}
				return;
			}
		} else if (current_focus_ == focus_target::window) {
			// Fallback to quit using Ctrl-C
			if (ev.key_code == 3) {
				logger.log("Ctrl-C pressed (Ignored for direct "
					   "quit, use ^KQ or ^KX)");
				return;
			}

			// Route to active window
			window *active_win = get_active_window();
			if (active_win) {
				active_win->get_queue().push(ev);
			}
		}
	}
}

void editor::render()
{
	clear();
	curs_set(0); // Default to hidden

	// Paint desktop background with dithered pattern
	attron(COLOR_PAIR(9));
	for (int y = 1; y < LINES - 1; ++y) {
		move(y, 0);
		for (int x = 0; x < COLS; ++x) {
			addstr("▒");
		}
	}
	attroff(COLOR_PAIR(9));

	// 2. Windows (Z-Index Managed)
	window *active_win = get_active_window();
	
	// Create a list of raw pointers to sort
	std::vector<window*> sorted_windows;
	for (auto& w : windows_) {
		sorted_windows.push_back(w.get());
	}

	// Sort: highest priority first. If priority is equal, newest timestamp first.
	std::sort(sorted_windows.begin(), sorted_windows.end(), [active_win](window* a, window* b) {
		int priority_a = (a == active_win) ? 9999 : a->get_display_priority();
		int priority_b = (b == active_win) ? 9999 : b->get_display_priority();
		
		if (priority_a != priority_b) {
			return priority_a > priority_b;
		}
		return a->get_last_active_timestamp() > b->get_last_active_timestamp();
	});

	// Occlusion culling: Assume visible, hide if fully covered by earlier (higher priority) windows.
	// For simplicity in this text editor (and since windows are mostly rectangles), 
	// a window is fully occluded if it's completely inside another single window above it.
	for (size_t i = 0; i < sorted_windows.size(); ++i) {
		window* w = sorted_windows[i];
		w->set_visible(true); // Default to visible
		
		// Check if fully covered by any single window above it
		for (size_t j = 0; j < i; ++j) {
			window* above = sorted_windows[j];
			if (above->is_visible() && 
			    w->get_x() >= above->get_x() && 
			    w->get_y() >= above->get_y() &&
			    w->get_x() + w->get_width() <= above->get_x() + above->get_width() &&
			    w->get_y() + w->get_height() <= above->get_y() + above->get_height()) {
				w->set_visible(false);
				break;
			}
		}
	}

	// Draw backwards (lowest priority first, so highest priority is on top)
	for (auto it = sorted_windows.rbegin(); it != sorted_windows.rend(); ++it) {
		if ((*it)->is_visible()) {
			(*it)->draw();
		}
	}

	top_menu_.draw();

	std::string debug_out;
	int cur_x = -1, cur_y = -1;

	if (debug_mode_) {
		auto &logger = event_logger::get_instance();
		auto msg = logger.get_latest_matching_message(debug_string_);
		if (msg) {
			debug_out = ">>" + *msg + "<<";
		}
	}

	if (active_win) {
		cur_x = active_win->get_cursor_x();
		cur_y = active_win->get_cursor_y();
	}

	std::string status_help = debug_out;
	if (k_block_mode_) {
		status_help = "K-Block: B:Beg K:End Y:Del C:Copy M:Move U:Top "
			      "V:End Q:Quit X:SaveExit F:Find";
	} else if (q_block_mode_) {
		status_help = "Q-Block: F:Find A:Replace";
	} else if (is_searching_prompt_) {
		std::string suggestion = get_search_autocomplete();
		if (!suggestion.empty() && suggestion != search_input_buffer_) {
			status_help = "Search for: " + search_input_buffer_ + "[" + suggestion.substr(search_input_buffer_.length()) + "]";
		} else {
			status_help = "Search for: " + search_input_buffer_ + "_";
		}
	} else if (is_search_options_prompt_) {
		status_help = "Options (I R B K): " + search_options_buffer_ + "_";
	} else if (is_going_to_line_prompt_) {
		status_help = "Go to line: " + line_input_buffer_ + "_";
	}

	std::string diag_text = "";
	if (active_win && active_win->get_document()) {
		auto doc = active_win->get_document();
		for (const auto& diag : doc->get_lsp_diagnostics()) {
			if (cur_y > diag.range.start_y && cur_y < diag.range.end_y) {
				diag_text = diag.message; break;
			} else if (cur_y == diag.range.start_y && cur_y == diag.range.end_y) {
				if (cur_x >= diag.range.start_x && cur_x <= diag.range.end_x) {
					diag_text = diag.message; break;
				}
			} else if (cur_y == diag.range.start_y && cur_x >= diag.range.start_x) {
				diag_text = diag.message; break;
			} else if (cur_y == diag.range.end_y && cur_x <= diag.range.end_x) {
				diag_text = diag.message; break;
			}
		}
		
		// If no LSP diagnostic, check for GCC build errors
		if (diag_text.empty()) {
			auto build_err = build_error_manager::get_instance().find_error_at(doc->get_filename(), cur_y);
			if (build_err) {
				diag_text = (build_err->is_warning ? "[Warning] " : "[Error] ") + build_err->message;
			}
		}
	}

	std::string combined_hover = hover_text_;
	if (!diag_text.empty()) {
		if (!combined_hover.empty()) combined_hover += " | ";
		combined_hover += diag_text;
	}

	bottom_status_.draw(status_help, combined_hover, cur_x, cur_y);

	if (active_dialog_) {
		active_dialog_->draw();
	}

	// Only show cursor if we are in window focus and NOT in a modal state
	if (current_focus_ == focus_target::window && !active_dialog_ && !k_block_mode_) {
		if (active_win) {
			active_win->set_cursor_position();
			curs_set(1);
		}
	}

	refresh();
}
