#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <lsp/json/json.h>
#include <ncurses.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include "agentlib/ai_agent.h"
#include "agentlib/copilot_manager.h"
#include "build_error_manager.h"
#include "codereview_manager.h"
#include "command_runner.h"
#include "config_manager.h"
#include "editor.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "help_text.h"
#include "history_manager.h"
#include "lsp_manager.h"
#include "pluginloader.h"
#include "project_manager.h"
#include "ui/agent_window.h"
#include "ui/code_review_window.h"
#include "ui/dialog_factories.h"
#include "ui/terminal_window.h"

namespace fs = std::filesystem;

static int find_free_port()
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		return 1234;

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
		socklen_t len = sizeof(addr);
		if (getsockname(sock, (struct sockaddr *)&addr, &len) == 0) {
			int port = ntohs(addr.sin_port);
			close(sock);
			return port;
		}
	}
	close(sock);
	return 1234;
}

void editor::dispatch_event_ui(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::terminate_run) {
		bool res = terminate_run(ev.key_code);
		if (ev.generic_promise) {
			auto prom = std::static_pointer_cast<std::promise<bool>>(ev.generic_promise);
			prom->set_value(res);
		}
		return;
	}

	if (ev.type == event_type::agent_start_app) {
		auto res = start_app(ev.payload, ev.alt_pressed, ev.auto_continue);
		if (ev.generic_promise) {
			auto prom = std::static_pointer_cast<std::promise<agentlib::start_app_result>>(ev.generic_promise);
			prom->set_value(res);
		}
		return;
	}

	if (ev.type == event_type::force_quit) {
		logger.log("Dispatching force_quit event.");

		bool any_dirty = false;
		for (const auto &doc : documents_) {
			if (doc->is_modified()) {
				any_dirty = true;
				break;
			}
		}

		if (any_dirty) {
			active_dialog_ = create_force_quit_dialog();
			active_dialog_mode_ = dialog_mode::force_quit_prompt;
			set_focus(focus_target::dialog, "force_quit");
			return;
		}

		logger.log("Application exit requested (source: force_quit clean).");
		is_running_ = false;
		return;
	}

	if (ev.type == event_type::quit) {
		logger.log("Dispatching quit event.");
		is_quitting_ = true;

		// If no windows, just exit
		if (windows_.empty()) {
			logger.log("Application exit requested (source: quit clean, no windows).");
			is_running_ = false;
			return;
		}

		// Check if any documents are modified. If so, prompt for the first modified document.
		for (const auto &doc : documents_) {
			if (doc && doc->is_modified() && !doc->is_read_only()) {
				std::string fname = doc->get_filename();
				if (fname.empty())
					fname = "untitled.txt";

				// CRITICAL SAVE PROMPT TARGET RESOLUTION:
				// When prompting the user to save/discard a modified document during exit, the window
				// displaying that document MUST be activated first. This ensures that get_active_doc()
				// correctly returns the targeted dirty document when resolve_dialog() is executed to
				// process the dialog's confirmation or discard action.
				for (size_t i = 0; i < windows_.size(); ++i) {
					if (windows_[i]->get_document() == doc) {
						activate_window(i);
						break;
					}
				}

				// Make sure we only prompt if we aren't already prompting
				if (active_dialog_mode_ != dialog_mode::save_prompt) {
					active_dialog_ = create_save_prompt_dialog(fname);
					active_dialog_mode_ = dialog_mode::save_prompt;

					// Important: pass a flag in the dialog payload so we know we are in a quit loop
					set_focus(focus_target::dialog, "quit_all");
				}
				return;
			}
		}

		// If no documents are modified (or they have been saved/discarded), it is safe to exit.
		logger.log("Application exit requested (source: quit clean, no dirty docs).");
		is_running_ = false;
		return;
	}

	if (ev.type == event_type::redraw) {
		logger.log("Dispatching redraw event.");
		return;
	}

	if (ev.type == event_type::notify_undo_changed) {
		logger.log("Dispatching notify_undo_changed to all windows.");
		for (auto &win : windows_) {
			win->get_window_queue().push(ev);
		}
		return;
	}

	if (ev.type == event_type::about) {
		logger.log("Dispatching about event.");
		active_dialog_ = create_welcome_dialog();
		active_dialog_mode_ = dialog_mode::welcome;
		set_focus(focus_target::dialog, "welcome");
		return;
	}

	if (ev.type == event_type::plugins) {
		logger.log("Dispatching plugins event.");
		std::vector<std::string> lines;
		const auto &plugins = plugin_loader::get_instance().get_plugins();
		if (plugins.empty()) {
			lines.push_back("No plugins loaded.");
		} else {
			for (size_t idx = 0; idx < plugins.size(); ++idx) {
				const auto &p = plugins[idx];
				lines.push_back(p.name + " (" + p.filename + ")");
				if (!p.description.empty()) {
					lines.push_back("  " + p.description);
				}
				if (idx + 1 < plugins.size()) {
					lines.push_back("");
				}
			}
		}
		active_dialog_ = create_message_dialog("Loaded Plugins", lines);
		set_focus(focus_target::dialog, "menu_plugins");
		return;
	}

	if (ev.type == event_type::tool_status) {
		logger.log("Dispatching tool_status event.");
		active_dialog_ = create_tool_status_dialog();
		set_focus(focus_target::dialog, "menu_tool_status");
		return;
	}

	if (ev.type == event_type::help) {
		logger.log("Dispatching help event.");
		// Check if "Help" window already exists
		for (size_t i = 0; i < windows_.size(); ++i) {
			if (windows_[i]->get_title() == "Help") {
				activate_window(i);
				return;
			}
		}

		auto doc = std::make_shared<document>(global_queue_, "Help");

		std::stringstream ss(help_text_md);
		std::string line;
		while (std::getline(ss, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			doc->append_line(line);
		}

		doc->set_read_only(true);
		documents_.push_back(doc);

		auto win = std::make_unique<window>(static_cast<int>(windows_.size() + 1), 0, 1, COLS, LINES - 2, "Help");
		win->attach_document(doc);
		windows_.push_back(std::move(win));
		activate_window(windows_.size() - 1);
		return;
	}

	if (ev.type == event_type::settings) {
		logger.log("Dispatching settings event.");
		active_dialog_ = create_settings_dialog();
		active_dialog_mode_ = dialog_mode::settings;
		set_focus(focus_target::dialog, "settings");
		return;
	}

	if (ev.type == event_type::image_manager) {
		logger.log("Dispatching image_manager event.");
		active_dialog_ = create_image_manager_dialog();
		active_dialog_mode_ = dialog_mode::image_manager;
		set_focus(focus_target::dialog, "image_manager");
		return;
	}

	if (ev.type == event_type::syntax_colors_config) {
		logger.log("Dispatching syntax_colors_config event.");
		active_dialog_ = create_syntax_colors_dialog();
		active_dialog_mode_ = dialog_mode::syntax_colors;
		set_focus(focus_target::dialog, "syntax_colors");
		return;
	}

	if (ev.type == event_type::task_models_config) {
		logger.log("Dispatching task_models_config event.");
		active_dialog_ = create_task_models_dialog();
		active_dialog_mode_ = dialog_mode::task_models;
		set_focus(focus_target::dialog, "task_models");
		return;
	}

	if (ev.type == event_type::run_settings) {
		logger.log("Dispatching run_settings event.");
		active_dialog_ = create_run_settings_dialog();
		active_dialog_mode_ = dialog_mode::run_settings;
		set_focus(focus_target::dialog, "run_settings");
		return;
	}

	if (ev.type == event_type::run_in_debugger) {
		logger.log("Dispatching run_in_debugger event.");
		std::string args = config_manager::get_instance().get_run_arguments();
		start_app(args, true);
		return;
	}

	if (ev.type == event_type::run_program) {
		logger.log("Dispatching run_program event.");
		std::string exe = config_manager::get_instance().get_main_executable();
		if (exe.empty()) {
			logger.log("run_program ignored: no main executable configured.");
			return;
		}

		std::string project_root = project_manager::get_instance().get_project_root();
		std::filesystem::path build_exe = std::filesystem::path(project_root) / "build" / exe;
		if (!std::filesystem::exists(build_exe)) {
			build_exe = std::filesystem::path(project_root) / exe;
			if (!std::filesystem::exists(build_exe)) {
				build_exe = exe;
			}
		}

		std::string args = config_manager::get_instance().get_run_arguments();

		std::string run_mode = config_manager::get_instance().get_run_target_mode();
		if (run_mode == "window") {
			start_app(args, false);
			return;
		}

		// Full screen mode fallback
		logger.log("Running main executable full screen: " + build_exe.string());

		// Configure command_runner to generate the sandboxed command line
		sync_command_runner runner;
		runner.apply_build_profile();
		runner.set_use_pty(true);
		runner.set_enable_crash_catcher(true);

		std::string raw_cmd = build_exe.string();
		if (!args.empty()) {
			raw_cmd += " " + args;
		}

		std::string sandboxed_cmd = runner.build_command(raw_cmd);
		logger.log("Executing sandboxed command: " + sandboxed_cmd);

		// 1. Temporarily pause ncurses
		def_prog_mode();
		endwin();

		// 2. Execute the sandboxed process via std::system
		int status = std::system(sandboxed_cmd.c_str());
		(void)status;

		// 3. User confirmation keypress
		std::printf("\n\r[Process completed. Press any key to return to editor...]");
		std::fflush(stdout);

		struct termios old_tio, new_tio;
		bool termios_ok = (tcgetattr(STDIN_FILENO, &old_tio) == 0);
		if (termios_ok) {
			new_tio = old_tio;
			new_tio.c_lflag &= ~(ICANON | ECHO);
			new_tio.c_cc[VMIN] = 1;
			new_tio.c_cc[VTIME] = 0;
			tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
		}

		char dummy;
		int r = read(STDIN_FILENO, &dummy, 1);
		(void)r;

		if (termios_ok) {
			tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
		}

		// 4. Resume curses
		reset_prog_mode();
		refresh();
		clear();

		// Trigger editor redraw
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return;
	}

	if (ev.type == event_type::models_config) {
		logger.log("Dispatching models_config event.");
		active_dialog_ = create_model_list_dialog();
		active_dialog_mode_ = dialog_mode::model_list;
		set_focus(focus_target::dialog, "model_list");
		return;
	}

	if (ev.type == event_type::model_servers_config) {
		logger.log("Dispatching model_servers_config event.");
		active_dialog_ = create_model_server_list_dialog();
		active_dialog_mode_ = dialog_mode::model_server_list;
		set_focus(focus_target::dialog, "server_list");
		return;
	}

	if (ev.type == event_type::mcp_config) {
		logger.log("Dispatching mcp_config event.");
		active_dialog_ = create_mcp_config_dialog();
		active_dialog_mode_ = dialog_mode::mcp_config;
		set_focus(focus_target::dialog, "mcp_config");
		return;
	}

	if (ev.type == event_type::copilot_connect) {
		logger.log("Dispatching copilot_connect event.");
		if (agentlib::copilot_manager::get_instance().is_authenticated()) {
			std::string error_msg;
			bool success = agentlib::copilot_manager::get_instance().fetch_and_register_github_models(error_msg);
			if (success) {
				active_dialog_ =
				    create_message_dialog("Copilot Connected", {"GitHub Copilot is already connected!",
										"AI models list has been updated successfully."});
			} else {
				active_dialog_ = create_message_dialog(
				    "Copilot Error", {"GitHub Copilot is connected, but failed", "to fetch models catalog:", error_msg});
			}
			active_dialog_mode_ = dialog_mode::none;
			set_focus(focus_target::dialog, "btn_ok");
			return;
		}
		active_dialog_ = create_copilot_connect_dialog();
		active_dialog_mode_ = dialog_mode::copilot_connect;
		set_focus(focus_target::dialog, "copilot_connect");
		return;
	}

	if (ev.type == event_type::agent_switch_model) {
		int target_id = ev.key_code;
		if (target_id == 0) {
			window *active_win = get_active_window();
			if (dynamic_cast<agent_window *>(active_win)) {
				target_id = active_win->get_id();
			}
		}

		if (target_id != 0) {
			logger.log("Dispatching agent_switch_model event for agent ID " + std::to_string(target_id));
			switching_agent_id_ = target_id;
			active_dialog_ = create_model_selection_dialog();
			active_dialog_mode_ = dialog_mode::model_selection;
			set_focus(focus_target::dialog, "model_selection");
		} else {
			logger.log("agent_switch_model ignored: no active agent window.");
		}
		return;
	}

	if (ev.type == event_type::open_agent) {
		logger.log("Dispatching open_agent event.");
		new_agent_window();
		return;
	}

	if (ev.type == event_type::agent_save_history) {
		logger.log("Dispatching agent_save_history event.");
		std::shared_ptr<agentlib::ai_agent> active_agent = nullptr;
		if (current_focus_ == focus_target::window || current_focus_ == focus_target::popup) {
			if (auto aw = dynamic_cast<agent_window *>(get_active_window())) {
				active_agent = aw->get_agent();
			}
		}
		if (active_agent) {
			std::string filepath = "tmp/agent_chat_" + std::to_string(active_agent->get_id()) + ".json";
			active_agent->save_conversation(filepath);
			editor_event status_ev;
			status_ev.type = event_type::set_transient_status;
			status_ev.payload = "Saved conversation to " + filepath;
			status_ev.priority = status_priorities::INFO;
			global_queue_.push(status_ev);
		} else {
			editor_event status_ev;
			status_ev.type = event_type::set_transient_status;
			status_ev.payload = "Error: No active agent window.";
			status_ev.priority = status_priorities::WARNING;
			global_queue_.push(status_ev);
		}
		return;
	}

	if (ev.type == event_type::open_crashdump_viewer) {
		logger.log("Dispatching open_crashdump_viewer event.");
		new_crashdump_window();
		return;
	}

	if (ev.type == event_type::open_agent_center) {
		logger.log("Dispatching open_agent_center event.");
		new_agent_center_window();
		return;
	}

	if (ev.type == event_type::open_codereview_viewer) {
		logger.log("Dispatching open_codereview_viewer event.");
		new_codereview_window(ev.key_code);
		return;
	}

	if (ev.type == event_type::codereview_action) {
		logger.log("Dispatching codereview_action event for item ID {} action {}", ev.key_code, ev.payload);
		auto item_opt = codereview_manager::get_instance().get_code_review_item(ev.key_code);
		if (item_opt) {
			codereview_edit_item_id_ = ev.key_code;
			if (ev.payload == "state") {
				std::vector<std::string> states = {"invalid", "new",	  "confirmed",	   "disputed",
								   "stale",   "resolved", "verified-fixed"};
				active_dialog_ = create_ask_user_dialog("Select State", states);
				active_dialog_mode_ = dialog_mode::codereview_select_state;
			} else if (ev.payload == "severity") {
				std::vector<std::string> severities = {"nit", "low", "medium", "high", "critical"};
				active_dialog_ = create_ask_user_dialog("Select Severity", severities);
				active_dialog_mode_ = dialog_mode::codereview_select_severity;
			} else if (ev.payload == "comment") {
				active_dialog_ = create_input_dialog("Add Comment", "Enter your comment text:", "");
				active_dialog_mode_ = dialog_mode::codereview_add_comment;
			} else if (ev.payload == "edit") {
				active_dialog_ = create_code_review_edit_dialog(*item_opt);
				active_dialog_mode_ = dialog_mode::codereview_edit_field;
			} else if (ev.payload == "reprocess") {
				auto item = *item_opt;
				std::shared_ptr<agentlib::ai_agent> main_agent = nullptr;
				for (auto &win : windows_) {
					if (auto aw = dynamic_cast<agent_window *>(win.get())) {
						if (auto agent = aw->get_agent()) {
							main_agent = agent;
							break;
						}
					}
				}

				std::shared_ptr<agentlib::ai_agent> verifier_agent = nullptr;
				if (main_agent) {
					verifier_agent = main_agent->spawn_subagent("Review Verifier");
				} else {
					auto default_model = agentlib::ai_model_registry::get_instance().get_default_model();
					verifier_agent =
					    agentlib::ai_agent::create(9999, "Review Verifier", default_model, &global_queue_, this);
					headless_agents_.push_back(verifier_agent);
				}

				if (verifier_agent) {
					std::string verifier_model_id = config_manager::get_instance().get_task_model_id("code_verifier");
					auto verifier_model = agentlib::ai_model_registry::get_instance().get_model(verifier_model_id);
					if (verifier_model) {
						verifier_agent->set_model(verifier_model);
					}
					verifier_agent->set_role(agentlib::agent_role::verifier);

					std::string system_prompt = "You are a code review verification agent. Your task is to verify the "
								    "code review findings reported by the reviewer agent.\n"
								    "Inspect the files and verify if the reported issues are correct, "
								    "transitioning them from 'new' to 'confirmed' or 'disputed'.\n"
								    "Also, if the developer claims to have resolved an issue, verify if "
								    "the fix is indeed correct and transition it to "
								    "'verified-fixed'.\n"
								    "Use the confirm_code_review_item tool to confirm/verify items, and "
								    "list_code_review_items to retrieve the list of items.\n";

					verifier_agent->inject_context("system",
								       project_manager::get_instance().get_project_knowledge_prompt());
					verifier_agent->inject_context("system", system_prompt);
					verifier_agent->inject_context(
					    "system", "Instructions for subagent: When you have completed your verification, call the "
						      "`agent_report_final_result` tool to report your final findings.");

					std::string task_prompt =
					    std::format("Please re-investigate and verify code review item #{} in file '{}' at line {}.\n"
							"Summary: {}\n"
							"Description: {}\n"
							"Proposed Fix: {}\n"
							"Current State: {}\n"
							"Please read the file, review the code, and determine if the issue is valid. "
							"Transition its state using the appropriate tool.",
							item.id, item.filename, item.line_number, item.summary, item.description,
							item.proposed_fix, item.state);

					// Submit the verification request to start the agent processing.
					verifier_agent->submit_prompt(task_prompt);
					// Immediately launch the subagent window to give the user real-time visibility
					// into the verification steps and tool invocations.
					open_subagent_window(verifier_agent);
					set_status_message(std::format("Verification agent started in background for item #{}...", item.id),
							   status_priorities::INFO);
				} else {
					set_status_message("Error: Failed to create verification agent.", status_priorities::WARNING);
				}
			}
		}
		return;
	}

	if (ev.type == event_type::open_subagent) {
		logger.log("Dispatching open_subagent event for agent ID: " + std::to_string(ev.key_code));

		int target_id = ev.key_code;

		// 1. Check if an agent window for this agent already exists
		for (size_t i = 0; i < windows_.size(); ++i) {
			if (auto aw = dynamic_cast<agent_window *>(windows_[i].get())) {
				if (aw->get_agent() && aw->get_agent()->get_id() == target_id) {
					activate_window(i);
					return;
				}
			}
		}

		// 2. Window doesn't exist, find the agent and spawn a window
		std::shared_ptr<agentlib::ai_agent> found_agent = nullptr;
		for (const auto &agent : get_all_active_agents()) {
			if (agent->get_id() == target_id) {
				found_agent = agent;
				break;
			}
		}

		if (found_agent) {
			open_subagent_window(found_agent);
		} else {
			logger.log("Error: Could not find agent with ID " + std::to_string(target_id) + " to open.");
		}
		return;
	}

	if (ev.type == event_type::agent_response) {
		logger.log("Dispatching agent_response event.");
		// Find the active agent window
		for (auto &win : windows_) {
			if (auto agent_win = dynamic_cast<agent_window *>(win.get())) {
				if (agent_win->get_agent() && agent_win->get_agent()->get_id() == ev.key_code) {
					agent_win->on_agent_update();
					break;
				}
			}
		}

		// Clean up headless agent if it exists
		headless_agents_.erase(std::remove_if(headless_agents_.begin(), headless_agents_.end(),
						      [&ev](const std::shared_ptr<agentlib::ai_agent> &agent) {
							      return agent && agent->get_id() == ev.key_code;
						      }),
				       headless_agents_.end());
		return;
	}

	if (ev.type == event_type::agent_tool_update) {
		logger.log("Dispatching agent_tool_update event.");
		// Find the active agent window
		for (auto &win : windows_) {
			if (auto agent_win = dynamic_cast<agent_window *>(win.get())) {
				if (agent_win->get_agent()->get_id() == ev.key_code) {
					agent_win->on_agent_update();
				}
			}
		}
		return;
	}

	if (ev.type == event_type::codereview_updated) {
		logger.log("Dispatching codereview_updated event for item ID {}", ev.key_code);
		// Update code review window list if open
		for (auto &win : windows_) {
			if (auto cr_win = dynamic_cast<code_review_window *>(win.get())) {
				cr_win->refresh();
			}
		}
		needs_full_redraw_ = true;

		auto item_opt = codereview_manager::get_instance().get_code_review_item(ev.key_code);
		if (item_opt) {
			std::string msg = std::format("Notification: Code review item created/updated (ID: {}):\n"
						      "- File: {}\n"
						      "- Line: {}\n"
						      "- Summary: {}\n"
						      "- Severity: {}\n"
						      "- Description: {}\n",
						      item_opt->id, item_opt->filename, item_opt->line_number, item_opt->summary,
						      item_opt->severity, item_opt->description);
			for (auto &win : windows_) {
				if (auto agent_win = dynamic_cast<agent_window *>(win.get())) {
					auto agent = agent_win->get_agent();
					if (agent) {
						agent->inject_context("user", msg, false);
					}
				}
			}
		}
		return;
	}

	if (ev.type == event_type::apply_edits) {
		logger.log("Dispatching apply_edits event.");
		// Payload is safe_path + "\n" + json
		size_t pos = ev.payload.find('\n');
		if (pos != std::string::npos) {
			std::string safe_path = ev.payload.substr(0, pos);
			std::string json_str = ev.payload.substr(pos + 1);

			// Find the document
			std::shared_ptr<document> target_doc = nullptr;
			for (const auto &doc : documents_) {
				std::string doc_path = doc->get_filename();
				if (!doc_path.empty()) {
					try {
						if (std::filesystem::weakly_canonical(doc_path).string() == safe_path) {
							target_doc = doc;
							break;
						}
					} catch (...) {
					}
				}
			}

			if (target_doc) {
				try {
					auto j = nlohmann::json::parse(json_str);
					if (j.is_array()) {
						target_doc->apply_external_edits_json(json_str);
					}
				} catch (...) {
					logger.log("Error parsing apply_edits json.");
				}
			}
		}
		return;
	}

	if (ev.type == event_type::prompt_user) {
		logger.log("Dispatching prompt_user event.");
		active_ask_user_promise_ = ev.prompt_promise;
		active_dialog_ = create_ask_user_dialog(ev.payload, ev.prompt_options);
		active_dialog_mode_ = dialog_mode::ask_user;
		set_focus(focus_target::dialog, "prompt_user");
		return;
	}

	if (ev.type == event_type::approve_plan) {
		logger.log("Dispatching approve_plan event.");
		active_ask_user_promise_ = ev.prompt_promise;
		active_dialog_ = create_plan_approval_dialog(ev.payload);
		active_dialog_mode_ = dialog_mode::approve_plan;
		set_focus(focus_target::dialog, "approve_plan");
		return;
	}

	if (ev.type == event_type::set_transient_status) {
		set_status_message(ev.payload, ev.priority, std::chrono::seconds(5));
		return;
	}

	if (ev.type == event_type::inline_agent_request) {
		launch_inline_agent(ev.payload);
		return;
	}
}

agentlib::start_app_result editor::start_app(const std::string &args, bool use_debugger, bool auto_continue)
{
	if (!is_main_thread()) {
		auto prom = std::make_shared<std::promise<agentlib::start_app_result>>();
		auto fut = prom->get_future();
		editor_event ev;
		ev.type = event_type::agent_start_app;
		ev.payload = args;
		ev.alt_pressed = use_debugger;
		ev.auto_continue = auto_continue;
		ev.generic_promise = prom;
		global_queue_.push(ev);
		return fut.get();
	}

	auto &logger = event_logger::get_instance();
	logger.log("start_app called with args: '" + args + "', debugger: " + (use_debugger ? "true" : "false"));

	std::string exe = config_manager::get_instance().get_main_executable();
	if (exe.empty()) {
		logger.log("start_app failed: no main executable configured.");
		return {-1, -1};
	}

	std::string project_root = project_manager::get_instance().get_project_root();
	std::filesystem::path build_exe = std::filesystem::path(project_root) / "build" / exe;
	if (!std::filesystem::exists(build_exe)) {
		build_exe = std::filesystem::path(project_root) / exe;
		if (!std::filesystem::exists(build_exe)) {
			build_exe = exe;
		}
	}

	// Clean up any existing terminal windows
	for (auto it = windows_.begin(); it != windows_.end();) {
		if ((*it)->get_title() == "Run Output" || (*it)->get_title() == "Debugger (GDB)") {
			if (auto tw = dynamic_cast<ui::terminal_window *>(it->get())) {
				tw->stop_process();
			}
			it = windows_.erase(it);
		} else {
			++it;
		}
	}

	agentlib::start_app_result result;

	if (use_debugger) {
		int port = find_free_port();
		logger.log("Found free port for GDBServer: " + std::to_string(port));

		int total_h = LINES - 2;
		int app_h = (total_h * 2) / 3;
		int gdb_h = total_h - app_h;

		int app_id = 1000 + static_cast<int>(windows_.size());
		int gdb_id = 1001 + static_cast<int>(windows_.size());

		auto app_tw = std::make_unique<ui::terminal_window>(app_id, 0, 1, COLS, app_h, "Run Output");
		app_tw->set_display_priority(10);
		auto gdb_tw = std::make_unique<ui::terminal_window>(gdb_id, 0, 1 + app_h, COLS, gdb_h, "Debugger (GDB)");
		gdb_tw->set_display_priority(10);
		gdb_tw->set_sanitize_recorded_data(true);
		app_tw->link_window(gdb_tw.get());

		// Generate a unique FIFO path in the project root directory (since /tmp is isolated in the sandbox)
		static std::atomic<unsigned int> fifo_counter{0};
		fs::path fifo_path = fs::path(project_root) / std::format(".turbostar_fifo_{}_{}_{}", getpid(), app_id, ++fifo_counter);
		if (mkfifo(fifo_path.c_str(), 0600) != 0) {
			logger.log(std::format("Failed to create input FIFO: {}", strerror(errno)));
		}

		std::string gdbserver_cmd =
		    "trap '' SIGTTOU SIGTTIN; exec gdbserver localhost:" + std::to_string(port) + " " + build_exe.string();
		if (!args.empty()) {
			gdbserver_cmd += " " + args;
		}
		gdbserver_cmd += " < " + fs_utils::escape_shell_arg(fifo_path.string()) + " 2>/dev/null";

		logger.log("Starting gdbserver: " + gdbserver_cmd);
		if (!app_tw->start_process(gdbserver_cmd, nullptr, true, false, config_manager::get_instance().is_shell_display_access())) {
			logger.log("Failed to start gdbserver process.");
			std::error_code ec;
			fs::remove(fifo_path, ec);
			return {-1, -1};
		}

		// Connect input FIFO to app_tw. This opens the FIFO for writing, unblocking the child process.
		app_tw->set_input_fifo(fifo_path.string());

		usleep(50000);

		std::string gdb_cmd = "exec gdb -q -ex \"set pagination off\" -ex \"set breakpoint pending on\" -ex \"target remote localhost:" + std::to_string(port) + "\"";
		if (auto_continue && config_manager::get_instance().get_gdb_auto_continue()) {
			gdb_cmd += " -ex \"continue\"";
		}
		gdb_cmd += " " + build_exe.string();

		logger.log("Starting gdb: " + gdb_cmd);
		if (!gdb_tw->start_process(gdb_cmd, nullptr, true, false)) {
			logger.log("Failed to start gdb process.");
			app_tw->stop_process();
			std::error_code ec;
			fs::remove(fifo_path, ec);
			return {-1, -1};
		}

		ui::terminal_window *gdb_tw_ptr = gdb_tw.get();

		result.app_run_id = app_id;
		result.gdb_run_id = gdb_id;

		windows_.push_back(std::move(app_tw));
		windows_.push_back(std::move(gdb_tw));

		update_window_layout();

		for (size_t i = 0; i < windows_.size(); ++i) {
			if (windows_[i].get() == gdb_tw_ptr) {
				activate_window(i);
				break;
			}
		}
		set_focus(focus_target::window, "start_app");
	} else {
		int app_id = 1000 + static_cast<int>(windows_.size());
		auto tw = std::make_unique<ui::terminal_window>(app_id, 0, 1, COLS, LINES - 2, "Run Output");
		tw->set_display_priority(10);

		std::string raw_cmd = build_exe.string();
		if (!args.empty()) {
			raw_cmd += " " + args;
		}

		logger.log("Starting app: " + raw_cmd);
		if (!tw->start_process(raw_cmd, nullptr, false, true, config_manager::get_instance().is_shell_display_access())) {
			logger.log("Failed to start app process.");
			return {-1, -1};
		}

		result.app_run_id = app_id;
		result.gdb_run_id = -1;

		windows_.push_back(std::move(tw));
		update_window_layout();
		activate_window(windows_.size() - 1);
		set_focus(focus_target::window, "start_app");
	}

	return result;
}

ui::terminal_window *editor::find_terminal_window(int run_id)
{
	for (auto &win : windows_) {
		if (win->get_id() == run_id) {
			return dynamic_cast<ui::terminal_window *>(win.get());
		}
	}
	return nullptr;
}

bool editor::write_to_run(int run_id, const std::string &data)
{
	auto *tw = find_terminal_window(run_id);
	if (!tw)
		return false;
	int fd = tw->get_pty_master_fd();
	if (fd < 0 || !tw->is_alive())
		return false;
	tw->reset_last_modified();
	ssize_t w = write(fd, data.data(), data.size());
	return (w == static_cast<ssize_t>(data.size()));
}

agentlib::run_screenshot_data editor::get_run_screenshot(int run_id)
{
	auto *tw = find_terminal_window(run_id);
	if (!tw)
		return {};
	auto snap = tw->get_screenshot();
	agentlib::run_screenshot_data res;
	res.grid = snap.grid;
	res.cursor_x = snap.cursor_x;
	res.cursor_y = snap.cursor_y;
	res.cursor_visible = snap.cursor_visible;
	return res;
}

int64_t editor::get_run_last_modified_age(int run_id)
{
	auto *tw = find_terminal_window(run_id);
	if (!tw)
		return -1;
	return tw->get_milliseconds_since_last_modification();
}

void editor::set_run_recording(int run_id, bool recording)
{
	auto *tw = find_terminal_window(run_id);
	if (tw) {
		tw->set_recording(recording);
	}
}

std::vector<std::string> editor::get_run_recorded_data(int run_id)
{
	auto *tw = find_terminal_window(run_id);
	if (tw) {
		return tw->get_recorded_data();
	}
	return {};
}

bool editor::terminate_run(int run_id)
{
	if (!is_main_thread()) {
		auto prom = std::make_shared<std::promise<bool>>();
		auto fut = prom->get_future();
		editor_event ev;
		ev.type = event_type::terminate_run;
		ev.key_code = run_id;
		ev.generic_promise = prom;
		global_queue_.push(ev);
		return fut.get();
	}

	std::vector<window *> to_close;
	for (auto it = windows_.begin(); it != windows_.end(); ++it) {
		if ((*it)->get_id() == run_id) {
			to_close.push_back(it->get());
			for (auto *linked : (*it)->get_linked_windows()) {
				to_close.push_back(linked);
			}
			break;
		}
	}

	if (to_close.empty())
		return false;

	for (auto *win : to_close) {
		if (auto tw = dynamic_cast<ui::terminal_window *>(win)) {
			if (tw->get_title() == "Debugger (GDB)") {
				int fd = tw->get_pty_master_fd();
				if (fd >= 0 && tw->is_alive()) {
					std::string quit_cmd = "quit\ny\n";
					// Ignore write result
					if (write(fd, quit_cmd.c_str(), quit_cmd.length()) < 0) {
					}
				}
			}
			tw->stop_process();
		}
		for (auto it = windows_.begin(); it != windows_.end();) {
			if (it->get() == win) {
				it = windows_.erase(it);
			} else {
				++it;
			}
		}
	}

	update_window_layout();
	return true;
}
