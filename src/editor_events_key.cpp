#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <lsp/json/json.h>
#include <ncurses.h>
#include <sstream>
#include "agentlib/ai_agent.h"
#include "agentlib/ai_model.h"
#include "agentlib/httplib_transport.h"
#include "build_error_manager.h"
#include "config_manager.h"
#include "editor.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "gcc_log_parser.h"
#include "history_manager.h"
#include "linux_clang_format.h"
#include "mcp/mcp_manager.h"
#include "project_manager.h"
#include "ui/agent_window.h"
#include "ui/dialog_factories.h"

namespace fs = std::filesystem;

void editor::resolve_dialog(dialog_result res)
{
	auto doc = get_active_doc();
	if (!doc && !documents_.empty())
		doc = documents_[0]; // Default fallback

	if (res == dialog_result::confirmed) {

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
		} else if (active_dialog_mode_ == dialog_mode::write_block) {
			std::string result_path = active_dialog_->get_result();
			if (doc) {
				doc->write_selection_to_file(result_path);
				history_manager::get_instance().add_file(result_path);
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
		} else if (active_dialog_mode_ == dialog_mode::ask_user || active_dialog_mode_ == dialog_mode::approve_plan) {
			if (active_ask_user_promise_) {
				active_ask_user_promise_->set_value(active_dialog_->get_result());
				active_ask_user_promise_.reset();
			}
		} else if (active_dialog_mode_ == dialog_mode::settings) {
			std::string res_str = active_dialog_->get_result();
			if (res_str == "ok" || res_str == "save_global") {
				apply_settings_from_dialog(*active_dialog_);

				// Handle Linux Kernel formatting style special case
				if (config_manager::get_instance().get_clang_format_style() == "Linux Kernel") {
					std::string project_root = project_manager::get_instance().get_project_root();
					std::filesystem::path format_file = std::filesystem::path(project_root) / ".clang-format";
					if (!std::filesystem::exists(format_file)) {
						std::ofstream out(format_file);
						if (out.is_open()) {
							out << LINUX_CLANG_FORMAT;
							event_logger::get_instance().log("Wrote Linux Kernel .clang-format to {}",
											 format_file.string());
						}
					}
				}

				if (res_str == "save_global") {
					config_manager::get_instance().save_global();
				} else {
					std::string cache_root = fs_utils::get_project_cache_root();
					if (!cache_root.empty()) {
						config_manager::get_instance().save_project(cache_root);
					} else {
						// Fallback to global if we are not in a project
						config_manager::get_instance().save_global();
					}
				}
			}
		} else if (active_dialog_mode_ == dialog_mode::run_settings) {
			std::string res_str = active_dialog_->get_result();
			if (res_str == "ok") {
				apply_run_settings_from_dialog(*active_dialog_);
				std::string cache_root = fs_utils::get_project_cache_root();
				if (!cache_root.empty()) {
					config_manager::get_instance().save_project(cache_root);
				} else {
					config_manager::get_instance().save_global();
				}
				update_window_menu();
			}
		} else if (active_dialog_mode_ == dialog_mode::model_list) {
			std::string res_str = active_dialog_->get_result();
			if (res_str == "add") {
				active_dialog_ = create_model_edit_dialog(nullptr);
				active_dialog_mode_ = dialog_mode::model_edit;
				editing_model_id_ = "";
				set_focus(focus_target::dialog, "model_edit");
				return;
			} else if (res_str.starts_with("edit:")) {
				std::string id = res_str.substr(5);
				editing_model_id_ = id;
				active_dialog_ = create_model_edit_dialog(agentlib::ai_model_registry::get_instance().get_model(id));
				active_dialog_mode_ = dialog_mode::model_edit;
				set_focus(focus_target::dialog, "model_edit");
				return;
			} else if (res_str.starts_with("delete:")) {
				std::string id = res_str.substr(7);
				agentlib::ai_model_registry::get_instance().remove_model(id);
				agentlib::ai_model_registry::get_instance().save_models();
				active_dialog_ = create_model_list_dialog();
				active_dialog_mode_ = dialog_mode::model_list;
				set_focus(focus_target::dialog, "model_list");
				return;
			} else if (res_str.starts_with("default:")) {
				std::string id = res_str.substr(8);
				config_manager::get_instance().set_default_model_id(id);

				std::string cache_root = fs_utils::get_project_cache_root();
				if (!cache_root.empty()) {
					config_manager::get_instance().save_project(cache_root);
				} else {
					config_manager::get_instance().save_global();
				}

				active_dialog_ = create_model_list_dialog();
				active_dialog_mode_ = dialog_mode::model_list;
				set_focus(focus_target::dialog, "model_list");
				return;
			} else if (res_str == "import") {
				auto url_opt = active_dialog_->get_value("server_url");
				std::string url = url_opt ? *url_opt : "";

				std::string error_msg;
				auto imported_models = agentlib::fetch_openai_models(url, error_msg);

				if (!imported_models.empty()) {
					auto &registry = agentlib::ai_model_registry::get_instance();
					for (const auto &model : imported_models) {
						registry.register_model(model);
					}
					registry.save_models();

					active_dialog_ = create_model_list_dialog();
					active_dialog_mode_ = dialog_mode::model_list;
					set_focus(focus_target::dialog, "model_list");
				} else {
					active_dialog_ =
					    create_message_dialog("Import Error", {"Failed to fetch models from server:", error_msg});
					active_dialog_mode_ = dialog_mode::model_list;
					set_focus(focus_target::dialog, "btn_ok");
				}
				return;
			} else if (res_str == "ok") {
				active_dialog_ = create_model_list_dialog();
				active_dialog_mode_ = dialog_mode::model_list;
				set_focus(focus_target::dialog, "model_list");
				return;
			}
		} else if (active_dialog_mode_ == dialog_mode::model_edit) {
			if (active_dialog_->get_result() == "ok") {
				apply_model_edit_from_dialog(*active_dialog_, editing_model_id_);
			}
			active_dialog_ = create_model_list_dialog();
			active_dialog_mode_ = dialog_mode::model_list;
			set_focus(focus_target::dialog, "model_list");
			return;
		} else if (active_dialog_mode_ == dialog_mode::model_selection) {
			std::string res_str = active_dialog_->get_result();
			if (res_str != "cancel" && switching_agent_id_ != -1) {
				auto new_model = agentlib::ai_model_registry::get_instance().get_model(res_str);
				if (new_model) {
					for (auto &win : windows_) {
						if (win->get_id() == switching_agent_id_) {
							auto awin = dynamic_cast<agent_window *>(win.get());
							if (awin && awin->get_agent()) {
								awin->get_agent()->set_model(new_model);
								break;
							}
						}
					}
				}
			}
			switching_agent_id_ = -1;
		} else if (active_dialog_mode_ == dialog_mode::mcp_config) {
			std::string res_str = active_dialog_->get_result();
			int current_idx = 0;
			auto idx_opt = active_dialog_->get_value("mcp_server_list");
			if (idx_opt) {
				try {
					current_idx = std::stoi(*idx_opt);
				} catch (...) {}
			}

			if (res_str.starts_with("toggle:")) {
				std::string name = res_str.substr(7);
				auto server = agentlib::mcp_manager::get_instance().find_server(name);
				if (server) {
					bool new_state = !server->is_enabled();
					agentlib::mcp_manager::get_instance().toggle_server(name, new_state);
					std::string project_root = project_manager::get_instance().get_project_root();
					agentlib::mcp_manager::get_instance().save_configs(project_root);
				}
				active_dialog_ = create_mcp_config_dialog(current_idx);
				active_dialog_mode_ = dialog_mode::mcp_config;
				set_focus(focus_target::dialog, "mcp_config");
				return;
			} else if (res_str.starts_with("tools:")) {
				std::string name = res_str.substr(6);
				configuring_mcp_server_ = name;
				active_dialog_ = create_mcp_tools_dialog(name);
				active_dialog_mode_ = dialog_mode::mcp_tools;
				set_focus(focus_target::dialog, "mcp_tools");
				return;
			}
		} else if (active_dialog_mode_ == dialog_mode::mcp_tools) {
			std::string res_str = active_dialog_->get_result();
			int current_idx = 0;
			auto idx_opt = active_dialog_->get_value("mcp_tool_list");
			if (idx_opt) {
				try {
					current_idx = std::stoi(*idx_opt);
				} catch (...) {}
			}

			if (res_str.starts_with("toggle:")) {
				std::string tool_name = res_str.substr(7);
				auto server = agentlib::mcp_manager::get_instance().find_server(configuring_mcp_server_);
				if (server) {
					bool new_state = true;
					for (const auto &tool : server->get_tools()) {
						if (tool.name == tool_name) {
							new_state = !tool.enabled;
							break;
						}
					}
					agentlib::mcp_manager::get_instance().toggle_tool(configuring_mcp_server_, tool_name, new_state);
					std::string project_root = project_manager::get_instance().get_project_root();
					agentlib::mcp_manager::get_instance().save_configs(project_root);
				}
				active_dialog_ = create_mcp_tools_dialog(configuring_mcp_server_, current_idx);
				active_dialog_mode_ = dialog_mode::mcp_tools;
				set_focus(focus_target::dialog, "mcp_tools");
				return;
			} else {
				int server_idx = 0;
				auto &manager = agentlib::mcp_manager::get_instance();
				const auto &servers = manager.get_servers();
				for (size_t i = 0; i < servers.size(); ++i) {
					if (servers[i]->get_name() == configuring_mcp_server_) {
						server_idx = static_cast<int>(i);
						break;
					}
				}
				active_dialog_ = create_mcp_config_dialog(server_idx);
				active_dialog_mode_ = dialog_mode::mcp_config;
				set_focus(focus_target::dialog, "mcp_config");
				return;
			}
		} else if (active_dialog_mode_ == dialog_mode::force_quit_prompt) {
			std::string res_str = active_dialog_->get_result();
			if (res_str == "exit") {
				event_logger::get_instance().log("Application exit requested (source: force_quit dialog).");
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
		} else if (active_dialog_mode_ == dialog_mode::reload_prompt) {
			std::string res_str = active_dialog_->get_result();
			active_dialog_.reset();
			active_dialog_mode_ = dialog_mode::none;
			set_focus(focus_target::window, "dialog_close");

			if (res_str == "yes") {
				if (doc && !doc->get_filename().empty()) {
					doc->load_from_file(doc->get_filename());
				}
			} else if (res_str == "never") {
				if (doc) {
					doc->set_ignore_disk_changes(true);
				}
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
				if (is_quitting_ && doc) {
					doc->clear_modified();
				}
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
		if (active_dialog_mode_ == dialog_mode::reload_prompt) {
			if (doc) {
				doc->update_last_disk_mtime();
			}
		}
		if (active_dialog_mode_ == dialog_mode::ask_user || active_dialog_mode_ == dialog_mode::approve_plan) {
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

		clear_status_message(status_priorities::HOVER); // Clear hover text on any input
		logger.log("Dispatching key_press event: " + std::to_string(ev.key_code));

		// 1. Modal Dialogs Interception
		//
		// CRITICAL IMPLEMENTATION DETAIL FOR MODALITY:
		// When a modal dialog (active_dialog_) is visible on the screen, it MUST have absolute
		// priority over all keyboard input. We check for an active dialog here, at the very
		// beginning of key dispatching, BEFORE checking global shortcuts (F1-F9, Alt-F3, etc.).
		//
		// Historically, global shortcuts were processed first. This meant that pressing F2 (Save)
		// or F9 (Compile) while a dialog was open would leak the keystroke to the document below,
		// triggering unexpected side-effects.
		//
		// By intercepting keys here and returning early, we guarantee that all keyboard events
		// are routed exclusively to the dialog controls (buttons, textboxes, lists). If the dialog
		// does not handle the key, it is safely swallowed rather than falling through to the editor.
		if (active_dialog_) {
			dialog_result res = active_dialog_->handle_key(ev.key_code);
			if (res != dialog_result::pending) {
				resolve_dialog(res);
			}
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
			return;
		}

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
			if (!doc || doc->is_read_only()) {
				logger.log("Cannot save read-only or empty buffer.");
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
		if (ev.key_code == KEY_F(6)) {
			logger.log("F6 shortcut pressed: toggling terminal window focus.");
			int run_idx = -1;
			int gdb_idx = -1;
			for (size_t i = 0; i < windows_.size(); ++i) {
				if (windows_[i]->get_title() == "Run Output") {
					run_idx = static_cast<int>(i);
				}
				if (windows_[i]->get_title() == "Debugger (GDB)") {
					gdb_idx = static_cast<int>(i);
				}
			}
			if (run_idx != -1 && gdb_idx != -1) {
				window *active_win = get_active_window();
				if (active_win == windows_[run_idx].get()) {
					activate_window(gdb_idx);
				} else if (active_win == windows_[gdb_idx].get()) {
					activate_window(run_idx);
				}
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
				return;
			}
		}
		if (ev.key_code == -static_cast<int>(KEY_F(3))) {
			editor_event close_ev;
			close_ev.type = event_type::close_window;
			global_queue_.push(close_ev);
			return;
		}



		// 1.5 Vim command prompt
		if (active_mode_ == input_mode::vim) {
			if (ev.key_code == 27 || ev.key_code == 3) { // ESC or Ctrl-C
				active_mode_ = input_mode::normal;
				vim_input_buffer_.clear();
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
				return;
			}
			if (ev.key_code == 13 || ev.key_code == 10 || ev.key_code == KEY_ENTER) {
				active_mode_ = input_mode::normal;
				execute_vim_command(vim_input_buffer_);
				vim_input_buffer_.clear();
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
				return;
			}
			if (ev.key_code == KEY_BACKSPACE || ev.key_code == 127 || ev.key_code == 8) {
				if (!vim_input_buffer_.empty()) {
					vim_input_buffer_.pop_back();
				} else {
					active_mode_ = input_mode::normal;
				}
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
				return;
			}
			if (!ev.utf8_char.empty() && ev.key_code >= 32 && ev.key_code < 127) {
				vim_input_buffer_ += ev.utf8_char;
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
				return;
			}
			return; // Consume all keys in vim prompt mode
		}

		// 2. Status Bar Search Prompt
		if (active_mode_ == input_mode::searching) {
			if (ev.key_code == 27 || ev.key_code == 3) { // ESC or Ctrl-C
				active_mode_ = input_mode::normal;
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
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

				active_mode_ = input_mode::search_options;
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
		if (active_mode_ == input_mode::search_options) {
			if (ev.key_code == 27 || ev.key_code == 3) { // ESC or Ctrl-C
				active_mode_ = input_mode::normal;
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
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
					if (lc == 'b')
						current_search_.backward = true;
					if (lc == 'k')
						current_search_.selected_text_only = true;
					if (lc == 'i')
						current_search_.ignore_case = true;
					if (lc == 'r')
						is_replace = true;
				}

				history_manager::get_instance().add_search(current_search_.query);

				if (is_replace) {
					active_mode_ = input_mode::normal;
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
				active_mode_ = input_mode::normal;
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
					if (search_options_buffer_.find(c) == std::string::npos &&
					    search_options_buffer_.find(std::toupper(c)) == std::string::npos) {
						search_options_buffer_ += static_cast<char>(std::toupper(c));
					}
				}
				return;
			}
			return; // Consume all keys in prompt mode
		}

		// 3. Status Bar Go to Line Prompt
		if (active_mode_ == input_mode::going_to_line) {
			if (ev.key_code == 27) { // ESC
				active_mode_ = input_mode::normal;
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
				active_mode_ = input_mode::normal;
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

		if (active_mode_ == input_mode::inline_agent) {
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

		if (ev.key_code == 27) { // ESC key
			// Make sure no other prompt/dialog is active
			if (current_focus_ == focus_target::window && !active_dialog_ && !active_popup_ &&
			    active_mode_ == input_mode::normal) {
				logger.log("Entering Vim prefix mode via ESC.");
				vim_prefix_mode_ = true;
				return;
			}
		}

		if (vim_prefix_mode_) {
			vim_prefix_mode_ = false;
			if (ev.key_code == ':') {
				logger.log("Entering Vim Command mode.");
				active_mode_ = input_mode::vim;
				vim_input_buffer_ = "";
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
				return;
			}
		}

		if (active_mode_ == input_mode::k_block) {
			if (handle_k_block_key(ev.key_code)) {
				if (active_mode_ == input_mode::k_block) {
					active_mode_ = input_mode::normal;
				}
				return;
			}
			active_mode_ = input_mode::normal;
		}

		if (active_mode_ == input_mode::q_block) {
			if (handle_q_block_key(ev.key_code)) {
				if (active_mode_ == input_mode::q_block) {
					active_mode_ = input_mode::normal;
				}
				return;
			}
			active_mode_ = input_mode::normal;
		}

		if (active_mode_ == input_mode::p_block) {
			if (handle_p_block_key(ev.key_code)) {
				if (active_mode_ == input_mode::p_block) {
					active_mode_ = input_mode::normal;
				}
				return;
			}
			active_mode_ = input_mode::normal;
		}

		if (ev.key_code == 11) { // Ctrl-K
			logger.log("Entering K-block mode.");
			active_mode_ = input_mode::k_block;
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
			return;
		}

		if (ev.key_code == 17) { // Ctrl-Q
			logger.log("Entering Q-block mode.");
			active_mode_ = input_mode::q_block;
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
			return;
		}

		if (ev.key_code == 16) { // Ctrl-P
			logger.log("Entering P-block mode.");
			active_mode_ = input_mode::p_block;
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
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
					std::string max_label = active_win->is_maximized() ? "Restore" : "Maximize";
					char max_hotkey = active_win->is_maximized() ? 'R' : 'M';

					if (auto aw = dynamic_cast<agent_window *>(active_win)) {
						items.push_back(
						    {static_cast<int>(event_type::agent_save_history), "Save History", 'S', false});
						if (aw->is_sidebar_expanded()) {
							items.push_back(
							    {static_cast<int>(event_type::agent_hide_sidebar), "Hide status pane", 'H', false});
						} else {
							items.push_back(
							    {static_cast<int>(event_type::agent_show_sidebar), "Show status pane", 'H', false});
						}
						items.push_back(
						    {static_cast<int>(event_type::maximize_window), max_label, max_hotkey, false});
						items.push_back({0, "", 0, true});
						items.push_back({static_cast<int>(event_type::close_window), "Close", 'l', false});
					} else {
						items.push_back({static_cast<int>(event_type::save), "Save", 'S', false});
						items.push_back({static_cast<int>(event_type::git_add), "Git Add", 'G', false});
						items.push_back({static_cast<int>(event_type::compile_file), "Compile File", 'C', false});
						items.push_back(
						    {static_cast<int>(event_type::maximize_window), max_label, max_hotkey, false});
						items.push_back({0, "", 0, true});
						items.push_back({static_cast<int>(event_type::close_window), "Close", 'l', false});
					}

					active_popup_ =
					    std::make_unique<popup_menu>(active_win->get_popup_button_x(), active_win->get_y() + 1, items);
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
				if (!doc->get_filename().empty() &&
				    project_manager::get_instance().lsp_is_supported_file(doc->get_filename())) {
					auto scope = doc->get_enclosing_scope();
					bool outside = true;
					if (scope) {
						if (new_y >= scope->start_y && new_y <= scope->end_y) {
							// For performance, we could check X too, but lines are usually enough
							outside = false;
						}
					}

					if (outside) {
						project_manager::get_instance().lsp_request_selection_range(doc->get_filename(), new_y,
													    new_x);
					}
				}
			}
		}
	}
}

void editor::handle_inline_agent_prompt_key(int key)
{
	if (key == 27) { // ESC
		active_mode_ = input_mode::normal;
		return;
	}
	if (key == 13 || key == 10 || key == KEY_ENTER) {
		if (!inline_agent_input_buffer_.empty()) {
			launch_inline_agent(inline_agent_input_buffer_);
		}
		active_mode_ = input_mode::normal;
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
	if (!doc)
		return;

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
	} else if (!filename.empty() && project_manager::get_instance().lsp_is_supported_file(filename)) {
		// No cached scope, need to wait for LSP
		logger.log("No cached scope for inline agent. Querying LSP.");
		pending_inline_agent_task_ = {prompt, true};
		project_manager::get_instance().lsp_request_selection_range(filename, cur_y, cur_x);
		return;
	}

	// 2. Collect diagnostics in range
	std::string diagnostics_context;
	auto diags = doc->get_diagnostics();
	for (const auto &diag : diags) {
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

	std::string system_prompt = "You are a headless surgical coding assistant. You are assisting the user with file '" + filename +
				    "'.\n"
				    "The user's cursor or selection is in the target range: lines " +
				    std::to_string(start_line) + " to " + std::to_string(end_line) +
				    ".\n"
				    "Your task is to perform code transformations or reviews within this range.\n";

	system_prompt += project_manager::get_instance().get_project_knowledge_prompt();

	if (!diagnostics_context.empty()) {
		system_prompt += "\nThe following diagnostics (errors/warnings) are active near this range:\n" + diagnostics_context;
	}

	system_prompt +=
	    "\nInstructions:\n"
	    "1. Use tool calls to perform the task. Do NOT provide a conversational response text.\n"
	    "2. Use 'fs_read_lines' to read the target range and surrounding context if needed.\n"
	    "3. For code refactoring: Use 'fs_replace_lines' to modify the code in the target range. Prefer surgical edits.\n"
	    "4. For spell checking/reviews: Use 'flag_as_error' to highlight spelling and grammatical mistakes. "
	    "Use 'error' (is_warning=false) for spelling mistakes and 'warning' (is_warning=true) for grammatical improvements. "
	    "Include the suggested improvement in the 'error_string' argument.\n"
	    "5. Use 'agent_set_status' to provide brief status updates (e.g., 'Analyzing...', 'Applying fixes...').\n"
	    "6. When finished, provide a final status update (e.g., 'Reformatted function.') and then simply stop. Do not ask for further "
	    "instructions.";

	agent->inject_context("system", system_prompt);
	agent->submit_prompt(prompt);

	headless_agents_.push_back(agent);

	set_status_message("Agent: Thinking...", status_priorities::INFO, std::chrono::seconds(10));
}

void editor::execute_vim_command(const std::string &cmd_raw)
{
	auto &logger = event_logger::get_instance();
	logger.log("Executing Vim command: " + cmd_raw);

	std::string cmd = cmd_raw;
	cmd.erase(cmd.begin(), std::find_if(cmd.begin(), cmd.end(), [](unsigned char ch) { return !std::isspace(ch); }));
	cmd.erase(std::find_if(cmd.rbegin(), cmd.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), cmd.end());

	if (cmd.empty())
		return;

	if (cmd == "q") {
		bool any_modified = false;
		for (const auto &doc : documents_) {
			if (doc && doc->is_modified() && !doc->is_read_only()) {
				any_modified = true;
				break;
			}
		}
		if (any_modified) {
			editor_event status_ev;
			status_ev.type = event_type::set_transient_status;
			status_ev.payload = "Error: No write since last change (add ! to override)";
			status_ev.priority = status_priorities::WARNING;
			global_queue_.push(status_ev);
		} else {
			editor_event quit_ev;
			quit_ev.type = event_type::quit;
			global_queue_.push(quit_ev);
		}
	} else if (cmd == "q!") {
		is_running_ = false;
	} else if (cmd == "w") {
		editor_event save_ev;
		save_ev.type = event_type::save;
		global_queue_.push(save_ev);
	} else if (cmd == "wq") {
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc && active_doc->is_modified() && !active_doc->is_read_only()) {
			if (active_doc->has_nondefault_filename()) {
				active_doc->save();
				history_manager::get_instance().add_file(active_doc->get_filename());
				for (auto &w : windows_) {
					if (w->get_document() == active_doc) {
						w->set_title(active_doc->get_filename());
					}
				}
				update_window_menu();
			} else {
				// Untitled
				is_quitting_ = true;
				editor_event save_as_ev;
				save_as_ev.type = event_type::save_as;
				global_queue_.push(save_as_ev);
				return;
			}
		}
		editor_event quit_ev;
		quit_ev.type = event_type::quit;
		global_queue_.push(quit_ev);
	} else if (cmd == "wq!") {
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc && active_doc->is_modified() && !active_doc->is_read_only()) {
			if (active_doc->has_nondefault_filename()) {
				active_doc->save();
			}
		}
		is_running_ = false;
	} else {
		editor_event status_ev;
		status_ev.type = event_type::set_transient_status;
		status_ev.payload = "Error: Unknown command: " + cmd;
		status_ev.priority = status_priorities::WARNING;
		global_queue_.push(status_ev);
	}
}
