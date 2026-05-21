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

void editor::resolve_dialog(dialog_result res)
{
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
			for (auto &w : windows_) {
				if (w->get_document() == doc) {
					w->set_title(doc->get_filename());
					break;
				}
			}
			update_window_menu();
			if (config_manager::get_instance().is_compile_on_save()) {
				editor_event compile_ev;
				compile_ev.type = event_type::compile_file;
				global_queue_.push(compile_ev);
			}
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
		} else if (active_dialog_mode_ == dialog_mode::ask_user) {
			if (active_ask_user_promise_) {
				active_ask_user_promise_->set_value(active_dialog_->get_result());
				active_ask_user_promise_.reset();
			}
		} else if (active_dialog_mode_ == dialog_mode::settings) {
			auto s_dialog = dynamic_cast<settings_dialog *>(active_dialog_.get());
			if (s_dialog) {
				config_manager::get_instance().set_clang_format_style(s_dialog->get_selected_style());
				config_manager::get_instance().set_build_system(s_dialog->get_build_system());
				config_manager::get_instance().set_build_directory(s_dialog->get_build_directory());
				config_manager::get_instance().set_llm_url(s_dialog->get_llm_url());
				config_manager::get_instance().set_lsp_enabled(s_dialog->is_lsp_enabled());
				config_manager::get_instance().set_auto_open_error_files(s_dialog->is_auto_open_error_files());
				config_manager::get_instance().set_compile_on_save(s_dialog->is_compile_on_save());
				config_manager::get_instance().save();
			}
		} else if (active_dialog_mode_ == dialog_mode::force_quit_prompt) {
			std::string res_str = active_dialog_->get_result();
			if (res_str == "exit") {
				is_running_ = false;
			} else if (res_str == "save_all") {
				editor_event save_all_ev;
				save_all_ev.type = event_type::save_all;
				global_queue_.push(save_all_ev);

				editor_event quit_ev;
				quit_ev.type = event_type::force_quit;
				global_queue_.push(quit_ev);
			}

			active_dialog_.reset();
			active_dialog_mode_ = dialog_mode::none;
			if (res_str == "cancel") {
				set_focus(focus_target::window, "dialog_close");
			}
			return;
		} else if (active_dialog_mode_ == dialog_mode::save_prompt) {
			std::string res_str = active_dialog_->get_result();
			active_dialog_.reset();
			active_dialog_mode_ = dialog_mode::none;
			set_focus(focus_target::window, "dialog_close");

			if (res_str == "save") {
				if (doc->get_filename().empty()) {
					editor_event save_as_ev;
					save_as_ev.type = event_type::save_as;
					global_queue_.push(save_as_ev);
				} else {
					doc->save();
					editor_event close_ev;
					close_ev.type = event_type::close_window;
					global_queue_.push(close_ev);
				}
			} else if (res_str == "discard") {
				editor_event close_ev;
				close_ev.type = event_type::close_window;
				close_ev.key_code = 1;
				global_queue_.push(close_ev);
			} else if (res_str == "cancel") {
			}
			return;
		}

		active_dialog_.reset();
		active_dialog_mode_ = dialog_mode::none;
		set_focus(focus_target::window, "dialog_close");

	} else if (res == dialog_result::cancelled) {
		if (active_dialog_mode_ == dialog_mode::ask_user) {
			if (active_ask_user_promise_) {
				active_ask_user_promise_->set_value("");
				active_ask_user_promise_.reset();
			}
		}

		active_dialog_.reset();
		active_dialog_mode_ = dialog_mode::none;
		set_focus(focus_target::window, "dialog_cancel");
	}
}

void editor::dispatch_event_key(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::key_press) {
		hover_text_ = ""; // Clear hover text on any input
		logger.log("Dispatching key_press event: " + std::to_string(ev.key_code));
	
		// Global shortcuts
		if (ev.key_code == KEY_F(1)) {
			logger.log("Help shortcut pressed.");
			editor_event help_ev;
			help_ev.type = event_type::help;
			global_queue_.push(help_ev);
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
			if (res != dialog_result::pending) {
				resolve_dialog(res);
			}
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
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
			
			if (c == '=') {
				logger.log("Alt-= pressed. Spawning popup menu.");
				window *active_win = get_active_window();
				if (active_win) {
					std::vector<popup_menu_item> items;
					items.push_back({static_cast<int>(event_type::save), "Save", 'S', false});
					items.push_back({static_cast<int>(event_type::git_add), "Git Add", 'G', false});
					items.push_back({static_cast<int>(event_type::compile_file), "Compile File", 'C', false});
					items.push_back({0, "", 0, true});
					items.push_back({static_cast<int>(event_type::close_window), "Close", 'l', false});
					
					active_popup_ = std::make_unique<popup_menu>(active_win->get_popup_button_x(), active_win->get_y() + 1, items);
					set_focus(focus_target::popup, "alt_equal");
					
					editor_event redraw_ev;
					redraw_ev.type = event_type::redraw;
					global_queue_.push(redraw_ev);
				}
				return;
			}
			
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
	
		// Route based on focus (Windows/Menu Bar/Popup)
		if (current_focus_ == focus_target::popup && active_popup_) {
			auto res = active_popup_->handle_key(ev.key_code);
			if (res.has_value()) {
				int id = res.value();
				active_popup_.reset();
				set_focus(focus_target::window, "popup_close");
				
				if (id != popup_menu::cancel_id) {
					editor_event action_ev;
					action_ev.type = static_cast<event_type>(id);
					global_queue_.push(action_ev);
				}
				
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
			}
			return; // Consume all keys when popup is focused
		} else if (current_focus_ == focus_target::menu_bar) {
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
