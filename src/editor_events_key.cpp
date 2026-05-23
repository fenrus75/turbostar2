#include "ui/dialog_factories.h"
#include "editor.h"
#include <algorithm>
#include <chrono>
#include <ncurses.h>
#include "event_logger.h"
#include "history_manager.h"
#include "config_manager.h"
#include "git_manager.h"
#include "lsp_manager.h"
#include "gcc_log_parser.h"
#include "build_error_manager.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "agentlib/ai_agent.h"
#include "agentlib/ai_model.h"
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
			if (is_quitting_) {
				editor_event quit_ev;
				quit_ev.type = event_type::quit;
				global_queue_.push(quit_ev);
			}
		} else if (active_dialog_mode_ == dialog_mode::search || active_dialog_mode_ == dialog_mode::replace) {
			current_search_ = extract_search_params(*active_dialog_, current_search_);
			history_manager::get_instance().add_search(current_search_.query);
			if (doc->find_next(current_search_)) {
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
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
			if (active_dialog_->get_result() == "ok") {
				apply_settings_from_dialog(*active_dialog_);
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
					if (is_quitting_) {
						editor_event quit_ev;
						quit_ev.type = event_type::quit;
						global_queue_.push(quit_ev);
					} else {
						editor_event close_ev;
						close_ev.type = event_type::close_window;
						global_queue_.push(close_ev);
					}
				}
			} else if (res_str == "discard") {
				editor_event close_ev;
				close_ev.type = event_type::close_window;
				close_ev.key_code = 1;
				global_queue_.push(close_ev);
				if (is_quitting_) {
					editor_event quit_ev;
					quit_ev.type = event_type::quit;
					global_queue_.push(quit_ev);
				}
			}
			return;
		}

		active_dialog_.reset();
		active_dialog_mode_ = dialog_mode::none;
		set_focus(focus_target::window, "dialog_close");

	} else if (res == dialog_result::cancelled) {
		is_quitting_ = false;
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
		std::shared_ptr<document> doc = get_active_doc();
		int old_y = -1, old_x = -1;
		if (doc) {
			old_y = doc->get_cursor_y();
			old_x = doc->get_cursor_x();
		}

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
			if (doc && doc->is_read_only()) {
				logger.log("Cannot save read-only buffer.");
				return;
			}
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
					active_dialog_ = create_search_dialog("Replace", current_search_, true);
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

		if (is_inline_agent_prompt_) {
			handle_inline_agent_prompt_key(ev.key_code);
			return;
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

		if (p_block_mode_) {
			if (handle_p_block_key(ev.key_code)) {
				p_block_mode_ = false;
				return;
			}
			p_block_mode_ = false;
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

		if (ev.key_code == 16) { // Ctrl-P
			logger.log("Entering P-block mode.");
			p_block_mode_ = true;
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

		// Background: Update LSP scope if cursor moved outside currently known scope
		if (doc) {
			int new_y = doc->get_cursor_y();
			int new_x = doc->get_cursor_x();
			if (new_y != old_y || new_x != old_x) {
				if (!doc->get_filename().empty() && lsp_manager::get_instance().is_supported_file(doc->get_filename())) {
					auto scope = doc->get_enclosing_scope();
					bool outside = true;
					if (scope) {
						if (new_y >= scope->start_y && new_y <= scope->end_y) {
							// For performance, we could check X too, but lines are usually enough
							outside = false;
						}
					}
					
					if (outside) {
						lsp_manager::get_instance().request_selection_range(doc->get_filename(), new_y, new_x);
					}
				}
			}
		}
	}

}

void editor::handle_inline_agent_prompt_key(int key)
{
	if (key == 27) { // ESC
		is_inline_agent_prompt_ = false;
		return;
	}
	if (key == 13 || key == 10 || key == KEY_ENTER) {
		if (!inline_agent_input_buffer_.empty()) {
			launch_inline_agent(inline_agent_input_buffer_);
		}
		is_inline_agent_prompt_ = false;
		return;
	}
	if (key == KEY_BACKSPACE || key == 127 || key == 8) {
		if (!inline_agent_input_buffer_.empty())
			inline_agent_input_buffer_.pop_back();
		return;
	}
	if (key >= 32 && key < 127) {
		inline_agent_input_buffer_ += static_cast<char>(key);
	}
}

void editor::launch_inline_agent(const std::string &prompt)
{
	auto &logger = event_logger::get_instance();
	logger.log("Launching inline agent with prompt: " + prompt);

	std::shared_ptr<document> doc = get_active_doc();
	if (!doc) return;

	int cur_y = doc->get_cursor_y();
	int cur_x = doc->get_cursor_x();
	std::string filename = doc->get_filename();

	// 1. Determine target range
	int start_line = cur_y + 1;
	int end_line = cur_y + 1;

	if (doc->has_selection()) {
		int sel_start_x, sel_start_y, sel_end_x, sel_end_y;
		doc->get_selection_range(sel_start_x, sel_start_y, sel_end_x, sel_end_y);
		start_line = sel_start_y + 1;
		end_line = sel_end_y + 1;
	} else if (doc->get_enclosing_scope()) {
		auto scope = doc->get_enclosing_scope();
		start_line = scope->start_y + 1;
		end_line = scope->end_y + 1;
	} else if (!filename.empty() && lsp_manager::get_instance().is_supported_file(filename)) {
		// No cached scope, need to wait for LSP
		logger.log("No cached scope for inline agent. Querying LSP.");
		pending_inline_agent_task_ = {prompt, true};
		lsp_manager::get_instance().request_selection_range(filename, cur_y, cur_x);
		return;
	}

	// 2. Collect diagnostics in range
	std::string diagnostics_context;
	auto diags = doc->get_diagnostics();
	for (const auto& diag : diags) {
		if (diag.range.start_y + 1 >= start_line - 5 && diag.range.start_y + 1 <= end_line + 5) {
			diagnostics_context += "- " + diag.message + " (line " + std::to_string(diag.range.start_y + 1) + ")\n";
		}
	}

	// 3. Spawn headless agent
	std::string model_id = config_manager::get_instance().get_default_model_id();
	auto model = agentlib::ai_model_registry::get_instance().get_model(model_id);
	if (!model) {
		logger.log("Error: Default model '" + model_id + "' not found in registry.");
		return;
	}

	int agent_id = static_cast<int>(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
	
	auto agent = agentlib::ai_agent::create(agent_id, "InlineAssistant", model, &global_queue_, this);
	
	std::string system_prompt = "You are a headless surgical coding assistant. You are assisting the user with file '" + filename + "'.\n"
		"The user's cursor or selection is in the target range: lines " + std::to_string(start_line) + " to " + std::to_string(end_line) + ".\n"
		"Your task is to perform code transformations or reviews within this range.\n";
	
	std::string project_instr = project_manager::get_instance().get_project_instructions();
	if (!project_instr.empty()) {
		system_prompt += "\nProject-specific instructions and engineering standards:\n" + project_instr + "\n";
	}

	if (!diagnostics_context.empty()) {
		system_prompt += "\nThe following diagnostics (errors/warnings) are active near this range:\n" + diagnostics_context;
	}

	system_prompt += "\nInstructions:\n"
		"1. Use tool calls to perform the task. Do NOT provide a conversational response text.\n"
		"2. Use 'fs_read_lines' to read the target range and surrounding context if needed.\n"
		"3. For code refactoring: Use 'fs_replace_lines' to modify the code in the target range. Prefer surgical edits.\n"
		"4. For spell checking/reviews: Use 'flag_as_error' to highlight spelling and grammatical mistakes. "
		"Use 'error' (is_warning=false) for spelling mistakes and 'warning' (is_warning=true) for grammatical improvements. "
		"Include the suggested improvement in the 'error_string' argument.\n"
		"5. Use 'agent_set_status' to provide brief status updates (e.g., 'Analyzing...', 'Applying fixes...').\n"
		"6. When finished, provide a final status update (e.g., 'Reformatted function.') and then simply stop. Do not ask for further instructions.";

	agent->inject_context("system", system_prompt);
	agent->submit_prompt(prompt);

	headless_agents_.push_back(agent);

	transient_status_message_ = "Agent: Thinking...";
	transient_status_expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);
}

