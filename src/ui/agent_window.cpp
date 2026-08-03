#include "ui/agent_window.h"
#include "agentlib/command_registry.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <iomanip>
#include <ncurses.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include "agentlib/httplib_transport.h"
#include "agentlib/skill_manager.h"
#include "agentlib/subagent_manager.h"
#include "ansi.h"
#include "config_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "input_history_manager.h"
#include "project_manager.h"
#include "utf8.h"
#include "filter_registry.h"
#include "ui/dynamic_colors.h"
#include "images/image_manager.h"
#include <filesystem>

using namespace agentlib;

agent_window::agent_window(int id, int x, int y, int width, int height, std::shared_ptr<agentlib::ai_model> model,
			   event_queue &global_queue, agentlib::document_provider *doc_provider, bool fresh_agent)
    : window(id, x, y, width, height, "Agent Chat")
{
	set_bypass_global_shortcuts(true);
	agent_ = ai_agent::create(id, "Agent", std::move(model), &global_queue, doc_provider);

	std::string system_prompt =
	    "You are an expert AI programming assistant.\n"
	    "Your goal is to help the user navigate, understand, and safely modify this codebase.\n"
	    "You have access to a suite of highly optimized, secure, and syntax-aware tools.\n"
	    "STRONGLY PREFER using built-in tools (e.g., fs_read_lines, fs_grep_files, fs_replace_lines, sqlite_perform, git_status, "
	    "git_diff_unstaged, git_diff_staged) over the generic "
	    "`run_shell_command` tool (e.g., use `fs_grep_files` instead of running `grep` via the shell, use `git_diff_unstaged` / "
	    "`git_diff_staged` instead of running `git diff` via the shell, and read `system://project/testlist.md` instead of `meson test --list` or "
	    "`ctest --show-only` via the shell).\n"
	    "Built-in tools are faster, automatically format their output for you, and do not require the user to manually approve a "
	    "security dialog for every action.\n"
	    "Only use `run_shell_command` when absolutely necessary for tasks that cannot be accomplished with built-in tools.\n\n"
	    "*** CRITICAL DIRECTIVE: VIRTUAL FILESYSTEM (VFS) ***\n"
	    "You have access to a Virtual Filesystem (VFS) to query resources through scheme-prefixed URIs.\n"
	    "IMPORTANT: VFS URIs are for tool use only (e.g., inside `fs_read_lines`, `fs_list_dir`) and are NOT accessible by generic "
	    "shell commands or Python scripts.\n\n"
	    "| Prefix | Description |\n"
	    "| :--- | :--- |\n"
	    "| `skills://` | Access custom tools, metadata, and specialized rule catalogs configured for the agent. |\n"
	    "| `system://` | Read system guidelines, workflow standards, project overview (`system://project/`), and subagent status/transcripts (`system://subagents/`). |\n"
	    "| `tmp://` | Scratch directory for the agent to store temporary files, diagnostic dumps, and intermediate run data to avoid cluttering the main project. |\n"
	    "| `github://` | Direct, cached HTTPS access to raw files, repository listings, and directory trees from GitHub (e.g., "
	    "github://username/project/). |\n";

	system_prompt += project_manager::get_instance().get_project_knowledge_prompt();

	// Auto-inject Primary Language System Guidelines from system:// VFS
	std::string prim_lang = config_manager::get_instance().get_primary_language();
	std::string vfs_lang_full_uri = "";
	std::string vfs_lang_short_uri = "";
	if (prim_lang == "C++" || prim_lang == "cpp") {
		vfs_lang_full_uri = "system://languages/cpp23.md";
		vfs_lang_short_uri = "system://languages/cpp23_short.md";
	} else if (prim_lang == "C" || prim_lang == "c") {
		vfs_lang_full_uri = "system://languages/c17.md";
		vfs_lang_short_uri = "system://languages/c17.md";
	} else if (prim_lang == "Python" || prim_lang == "py") {
		vfs_lang_full_uri = "system://languages/python311.md";
		vfs_lang_short_uri = "system://languages/python311.md";
	} else if (prim_lang == "Rust" || prim_lang == "rs") {
		vfs_lang_full_uri = "system://languages/rust2021.md";
		vfs_lang_short_uri = "system://languages/rust2021.md";
	} else if (prim_lang == "TypeScript" || prim_lang == "ts" || prim_lang == "JavaScript" || prim_lang == "js") {
		vfs_lang_full_uri = "system://languages/typescript.md";
		vfs_lang_short_uri = "system://languages/typescript.md";
	} else if (prim_lang == "Verilog" || prim_lang == "v") {
		vfs_lang_full_uri = "system://languages/verilog.md";
		vfs_lang_short_uri = "system://languages/verilog.md";
	} else {
		vfs_lang_full_uri = "system://languages/default.md";
		vfs_lang_short_uri = "system://languages/default_short.md";
	}

	bool has_agents_md = !project_manager::get_instance().get_project_instructions().empty();
	if (has_agents_md) {
		if (!vfs_lang_full_uri.empty()) {
			system_prompt += "\n\nLanguage guidelines available at " + vfs_lang_full_uri + "\n";
		}
	} else {
		if (!vfs_lang_short_uri.empty()) {
			agentlib::virtual_file_system vfs;
			auto lang_doc = vfs.read_file(vfs_lang_short_uri);
			if (lang_doc.has_value()) {
				system_prompt += "\n\n*** PRIMARY LANGUAGE GUIDELINES (" + prim_lang + ") ***\n";
				system_prompt += std::string((*lang_doc)->view());
			}
		}
	}

	agent_->inject_context("system", system_prompt);

	// Load the active state from the previous session if it exists.
	// This inherently gives us Cross-Session Persistence as per the design doc.
	if (agent_->load_active_state(fresh_agent)) {
		agent_->add_interaction(
		    std::make_shared<agentlib::interaction_system_message>("Agent state restored from previous session."));
	} else if (!fresh_agent) {
		agent_->inject_archived_episodes_summary();
	}

	set_background_color_pair(17); // Use cyan background to differentiate from normal editors

	input_box_ = std::make_unique<ui_multiline_edit>("input", 0, 0, width_ - 2, 3, [this, id](const std::string &text) {
		std::string trimmed_text = text;
		size_t first = trimmed_text.find_first_not_of(" \t\r\n");
		if (first != std::string::npos) {
			trimmed_text.erase(0, first);
			trimmed_text.erase(trimmed_text.find_last_not_of(" \t\r\n") + 1);
		} else {
			trimmed_text.clear();
		}

		if (trimmed_text.starts_with("/")) {
			input_box_->set_buffer("");
			std::string cmd_name;
			std::string args;
			size_t space_pos = trimmed_text.find(' ');
			if (space_pos != std::string::npos) {
				cmd_name = trimmed_text.substr(1, space_pos - 1);
				args = trimmed_text.substr(space_pos + 1);
				size_t first = args.find_first_not_of(" \t\r\n");
				if (first != std::string::npos) {
					args.erase(0, first);
					args.erase(args.find_last_not_of(" \t\r\n") + 1);
				} else {
					args.clear();
				}
			} else {
				cmd_name = trimmed_text.substr(1);
			}

			auto cmd = command_registry::get_instance().get_command(cmd_name);
			if (cmd) {
				agent_command::context ctx;
				ctx.agent = agent_.get();
				ctx.window_id = id;
				ctx.arguments = args;
				ctx.global_queue = agent_->get_global_queue() ? agent_->get_global_queue() : &get_queue();

				cmd->execute(ctx);
				scroll_offset_ = 0;
				invalidate();
				return;
			}

			agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>(
			    std::format("Unknown command: /{}. Type /help for a list of available commands.", cmd_name)));
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		agent_->submit_prompt(text);
		input_box_->set_buffer(""); // Clear the box
		scroll_offset_ = 0;
		invalidate();
	});

	input_box_->set_history_enabled(true, "agent_prompt");

	input_box_->set_on_change([this](const std::string &text) {
		if (text.starts_with("/")) {
			editor_event status_ev;
			status_ev.type = event_type::set_transient_status;
			status_ev.payload =
			    "Commands: /help /info /quit /save /stats /memory /episode /pageout /pagein /model /mcp /skills /compact /rescan /sysprompt /clear";
			status_ev.priority = status_priorities::INFO;
			if (agent_->get_global_queue()) {
				agent_->get_global_queue()->push(status_ev);
			} else {
				get_queue().push(status_ev);
			}
		} else if (text.empty() || text.length() == 1) {
			// Clear status when empty or back to 1 char without slash
			editor_event status_ev;
			status_ev.type = event_type::set_transient_status;
			status_ev.payload = "";
			status_ev.priority = status_priorities::INFO;
			if (agent_->get_global_queue()) {
				agent_->get_global_queue()->push(status_ev);
			} else {
				get_queue().push(status_ev);
			}
		}
	});


	auto &skills = skill_manager::get_instance().get_skills();
	std::string skills_text = "Available Skills:\n";
	int visible_count = 0;
	for (const auto &s : skills) {
		if (s.visible) {
			skills_text += "- " + s.name + "\n";
			visible_count++;
		}
	}
	if (visible_count > 0) {
		agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>(skills_text));
	}


	subagents_list_ = std::make_unique<ui_listbox>(
	    "subagents", 0, 0, 1, 1, [this](int) { invalidate(); },
	    [this](int index) {
		    auto subagents = get_active_subagents();
		    if (index >= 0 && index < (int)subagents.size()) {
			    editor_event sub_ev;
			    sub_ev.type = event_type::open_subagent;
			    sub_ev.key_code = subagents[index]->get_id();
			    if (agent_->get_global_queue()) {
				    agent_->get_global_queue()->push(sub_ev);
			    } else {
				    get_queue().push(sub_ev);
			    }
		    }
	    });
}

agent_window::agent_window(int id, int x, int y, int width, int height, std::shared_ptr<agentlib::ai_agent> existing_agent)
    : window(id, x, y, width, height, existing_agent->get_name()), agent_(std::move(existing_agent))
{
	set_bypass_global_shortcuts(true);
	set_background_color_pair(17);

	input_box_ = std::make_unique<ui_multiline_edit>("input", 0, 0, width_ - 2, 3, [this, id](const std::string &text) {
		std::string trimmed_text = text;
		size_t first = trimmed_text.find_first_not_of(" \t\r\n");
		if (first != std::string::npos) {
			trimmed_text.erase(0, first);
			trimmed_text.erase(trimmed_text.find_last_not_of(" \t\r\n") + 1);
		} else {
			trimmed_text.clear();
		}

		if (trimmed_text.starts_with("/")) {
			input_box_->set_buffer("");
			std::string cmd_name;
			std::string args;
			size_t space_pos = trimmed_text.find(' ');
			if (space_pos != std::string::npos) {
				cmd_name = trimmed_text.substr(1, space_pos - 1);
				args = trimmed_text.substr(space_pos + 1);
				size_t first = args.find_first_not_of(" \t\r\n");
				if (first != std::string::npos) {
					args.erase(0, first);
					args.erase(args.find_last_not_of(" \t\r\n") + 1);
				} else {
					args.clear();
				}
			} else {
				cmd_name = trimmed_text.substr(1);
			}

			auto cmd = command_registry::get_instance().get_command(cmd_name);
			if (cmd) {
				agent_command::context ctx;
				ctx.agent = agent_.get();
				ctx.window_id = id;
				ctx.arguments = args;
				ctx.global_queue = agent_->get_global_queue() ? agent_->get_global_queue() : &get_queue();

				cmd->execute(ctx);
				scroll_offset_ = 0;
				invalidate();
				return;
			}

			agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>(
			    std::format("Unknown command: /{}. Type /help for a list of available commands.", cmd_name)));
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		agent_->submit_prompt(text);
		input_box_->set_buffer(""); // Clear the box
		scroll_offset_ = 0;
		invalidate();
	});

	input_box_->set_history_enabled(true, "agent_prompt");

	input_box_->set_on_change([this](const std::string &text) {
		if (text.starts_with("/")) {
			editor_event status_ev;
			status_ev.type = event_type::set_transient_status;
			status_ev.payload =
			    "Commands: /help /info /quit /save /stats /memory /episode /pageout /pagein /model /mcp /skills /compact /rescan";
			status_ev.priority = status_priorities::INFO;
			if (agent_->get_global_queue()) {
				agent_->get_global_queue()->push(status_ev);
			} else {
				get_queue().push(status_ev);
			}
		} else if (text.empty() || text.length() == 1) {
			// Clear status when empty or back to 1 char without slash
			editor_event status_ev;
			status_ev.type = event_type::set_transient_status;
			status_ev.payload = "";
			status_ev.priority = status_priorities::INFO;
			if (agent_->get_global_queue()) {
				agent_->get_global_queue()->push(status_ev);
			} else {
				get_queue().push(status_ev);
			}
		}
	});



	subagents_list_ = std::make_unique<ui_listbox>(
	    "subagents", 0, 0, 1, 1, [this](int) { invalidate(); },
	    [this](int index) {
		    auto subagents = get_active_subagents();
		    if (index >= 0 && index < (int)subagents.size()) {
			    editor_event sub_ev;
			    sub_ev.type = event_type::open_subagent;
			    sub_ev.key_code = subagents[index]->get_id();
			    if (agent_->get_global_queue()) {
				    agent_->get_global_queue()->push(sub_ev);
			    } else {
				    get_queue().push(sub_ev);
			    }
		    }
	    });
}

agent_window::~agent_window()
{
	if (agent_) {
		agent_->close();
	}
}

bool agent_window::process_events()
{
	bool needs_render = false;

	while (auto ev = get_queue().pop()) {
		bool has_subagents = !get_active_subagents().empty();
		bool show_sidebar = sidebar_expanded_ && has_subagents;
		int sidebar_w = (width_ * 30) / 100;
		sidebar_w = std::max(15, std::min(sidebar_w, width_ - 20));
		int divider_x = width_ - 1 - sidebar_w;

		if (ev->type == event_type::key_press) {
			is_mouse_selecting_ = false;
			mouse_sel_start_char_ = -1;
			mouse_sel_start_line_ = -1;
			mouse_sel_end_char_ = -1;
			mouse_sel_end_line_ = -1;

			int key = ev->key_code;

			// Handle sidebar toggle via Ctrl-T (20)
			if (key == 20) {
				sidebar_expanded_ = !sidebar_expanded_;
				invalidate();
				needs_render = true;
				continue;
			}

			// Handle Tab focus cycling
			if (show_sidebar && key == 9) {
				if (sidebar_focus_ == sidebar_focus::input) {
					if (has_subagents) {
						sidebar_focus_ = sidebar_focus::subagents;
					}
				} else { // subagents
					sidebar_focus_ = sidebar_focus::input;
				}
				invalidate();
				needs_render = true;
				continue;
			}

			// Esc returns focus to input box
			if (show_sidebar && key == 27 && sidebar_focus_ != sidebar_focus::input) {
				sidebar_focus_ = sidebar_focus::input;
				invalidate();
				needs_render = true;
				continue;
			}

			if ((agent_->get_status() != agent_status::idle)) {
				if (key == 27) {
					agent_->cancel_current_task();
					needs_render = true;
					continue;
				}
			}

			// Route based on focus
			if (show_sidebar && sidebar_focus_ == sidebar_focus::subagents) {
				if (subagents_list_->handle_event(*ev, 0, 0)) {
					invalidate();
					needs_render = true;
					continue;
				}
			}

			// Default: Route to input box
			if (is_active() && input_box_ && input_box_->handle_event(*ev, 0, 0)) {
				scroll_offset_ = 0;
				needs_render = true;
				continue;
			}

			// Window-level scrolling fallback
			if (key == KEY_UP) { // Up
				scroll_offset_++;
				needs_render = true;
			} else if (key == KEY_DOWN) { // Down
				if (scroll_offset_ > 0) {
					scroll_offset_--;
					needs_render = true;
				}
			} else if (key == 21 || key == KEY_PPAGE) { // Ctrl-U or Page Up
				scroll_offset_ += get_content_height() - 3;
				needs_render = true;
			} else if (key == 22 || key == KEY_NPAGE) { // Ctrl-V or Page Down
				scroll_offset_ -= get_content_height() - 3;
				if (scroll_offset_ < 0)
					scroll_offset_ = 0;
				needs_render = true;
			} else if (key == KEY_END) { // End key: scroll to bottom
				scroll_offset_ = 0;
				needs_render = true;
			}
		} else if (ev->type == event_type::paste) {
			if (is_active() && input_box_ && input_box_->handle_event(*ev, 0, 0)) {
				scroll_offset_ = 0;
				needs_render = true;
			}
		} else if (ev->type == event_type::mouse_scroll_up || ev->type == event_type::mouse_scroll_down) {
			if (show_sidebar) {
				if (has_subagents) {
					int sub_y_start = y_ + 1;
					int sub_y_end = y_ + height_ - 2;
					if (ev->mouse_x > x_ + divider_x && ev->mouse_x < x_ + width_ - 1 && ev->mouse_y >= sub_y_start &&
					    ev->mouse_y <= sub_y_end) {
						editor_event sim_ev;
						sim_ev.type = event_type::key_press;
						sim_ev.key_code = (ev->type == event_type::mouse_scroll_up) ? KEY_UP : KEY_DOWN;
						subagents_list_->handle_event(sim_ev, 0, 0);
						invalidate();
						needs_render = true;
						continue;
					}
				}
			}

			// Fallback to chat history scroll
			if (ev->type == event_type::mouse_scroll_up) {
				scroll_offset_ += 3;
			} else {
				scroll_offset_ -= 3;
				if (scroll_offset_ < 0)
					scroll_offset_ = 0;
			}
			needs_render = true;
		} else if (ev->type == event_type::mouse_click) {
			// Clicked on right scrollbar?
			if (ev->mouse_x == x_ + width_ - 1 && ev->mouse_y >= y_ + 1 && ev->mouse_y <= y_ + height_ - 2) {
				if (max_scroll_offset_ > 0) {
					if (ev->mouse_y == y_ + 1) {
						// Click ▲
						scroll_offset_ = std::min(max_scroll_offset_, scroll_offset_ + 1);
					} else if (ev->mouse_y == y_ + height_ - 2) {
						// Click ▼
						scroll_offset_ = std::max(0, scroll_offset_ - 1);
					} else {
						// Click track
						int track_h = height_ - 4;
						if (track_h > 1) {
							double ratio = static_cast<double>(ev->mouse_y - (y_ + 2)) / (track_h - 1);
							// Note: ratio 0.0 is top (max_scroll_offset_), 1.0 is bottom (0)
							scroll_offset_ = static_cast<int>((1.0 - ratio) * max_scroll_offset_);
							scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll_offset_);
						}
					}
					invalidate();
					needs_render = true;
					continue;
				}
			}

			// Clicked on scroll to bottom/follow indicator?
			int max_width = show_sidebar ? (divider_x - 1) : (width_ - 2);
			if (scroll_offset_ > 0) {
				int overlay_y = y_ + height_ - 6;
				int overlay_x = x_ + max_width - 12;
				if (ev->mouse_y == overlay_y && ev->mouse_x >= overlay_x && ev->mouse_x <= overlay_x + 9) {
					scroll_offset_ = 0;
					invalidate();
					needs_render = true;
					continue;
				}
			}

			if (show_sidebar) {
				// 1. Clicked on collapse button on vertical divider?
				if (ev->mouse_x == x_ + divider_x && ev->mouse_y == y_ + 2) {
					sidebar_expanded_ = false;
					invalidate();
					needs_render = true;
					continue;
				}

				// 2. Clicked inside Subagents?
				if (has_subagents) {
					int sub_y_start = y_ + 1;
					int sub_y_end = y_ + height_ - 2;
					if (ev->mouse_x > x_ + divider_x && ev->mouse_x < x_ + width_ - 1 && ev->mouse_y >= sub_y_start &&
					    ev->mouse_y <= sub_y_end) {
						sidebar_focus_ = sidebar_focus::subagents;
						subagents_list_->handle_event(*ev, 0, 0);
						invalidate();
						needs_render = true;
						continue;
					}
				}
			} else if (has_subagents) {
				// Clicked on expand button on right border?
				if (ev->mouse_x == x_ + width_ - 1 && ev->mouse_y == y_ + 2) {
					sidebar_expanded_ = true;
					invalidate();
					needs_render = true;
					continue;
				}
			}

			// Clicked inside chat history?
			int chat_max_x = show_sidebar ? (x_ + divider_x - 1) : (x_ + width_ - 2);
			int available_height = get_history_viewport_height();
			int click_row = ev->mouse_y - y_ - 1;
			if (click_row >= 0 && click_row < available_height && click_row < static_cast<int>(visible_lines_.size()) &&
			    ev->mouse_x >= x_ + 1 && ev->mouse_x <= chat_max_x) {
				sidebar_focus_ = sidebar_focus::input;
				int prefix_w =
				    visible_lines_[click_row].prefix.empty() ? 0 : utf8::display_width(visible_lines_[click_row].prefix);
				int col = ev->mouse_x - (x_ + 1 + prefix_w);
				int click_char = std::clamp(col, 0, static_cast<int>(utf8::display_width(visible_lines_[click_row].text)));

				is_mouse_selecting_ = true;
				mouse_sel_start_char_ = click_char;
				mouse_sel_start_line_ = click_row;
				mouse_sel_end_char_ = click_char;
				mouse_sel_end_line_ = click_row;
				needs_render = true;
			} else {
				is_mouse_selecting_ = false;
				mouse_sel_start_char_ = -1;
				mouse_sel_start_line_ = -1;
				mouse_sel_end_char_ = -1;
				mouse_sel_end_line_ = -1;
				// If clicked in input box area, focus input
				if (ev->mouse_y >= y_ + height_ - 4) {
					sidebar_focus_ = sidebar_focus::input;
				}
				needs_render = true;
			}
		} else if (ev->type == event_type::mouse_drag) {
			if (is_mouse_selecting_) {
				int available_height = get_history_viewport_height();
				int click_row = ev->mouse_y - y_ - 1;
				click_row = std::clamp(click_row, 0, available_height - 1);
				if (click_row >= 0 && click_row < static_cast<int>(visible_lines_.size())) {
					int prefix_w = visible_lines_[click_row].prefix.empty()
							   ? 0
							   : utf8::display_width(visible_lines_[click_row].prefix);
					int col = ev->mouse_x - (x_ + 1 + prefix_w);
					int click_char =
					    std::clamp(col, 0, static_cast<int>(utf8::display_width(visible_lines_[click_row].text)));

					mouse_sel_end_char_ = click_char;
					mouse_sel_end_line_ = click_row;
					needs_render = true;
				}
			}
		} else if (ev->type == event_type::mouse_release) {
			if (is_mouse_selecting_) {
				std::string selected_text = get_mouse_selected_text();
				if (!selected_text.empty()) {
					ansi::copy_to_clipboard(selected_text);
				}
				is_mouse_selecting_ = false;
				needs_render = true;
			}
		}
	}

	scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll_offset_);

	if (needs_render)
		invalidate();
	return needs_render;
}

void agent_window::on_agent_update()
{
	// Only snap to bottom if we were already at the bottom (follow mode active)
	if (scroll_offset_ == 0) {
		scroll_offset_ = 0;
	}
	invalidate();
}

void agent_window::draw_content(bool /*cursor_only*/) const
{
	// 1. Draw the chat history in the upper portion
	bool has_subagents = !get_active_subagents().empty();
	bool show_sidebar = sidebar_expanded_ && has_subagents;
	int sidebar_w = (width_ * 30) / 100;
	sidebar_w = std::max(15, std::min(sidebar_w, width_ - 20));
	int divider_x = width_ - 1 - sidebar_w;
	int abs_divider_x = x_ + divider_x;

	int available_height = get_history_viewport_height();
	visible_lines_.clear();
	visible_lines_.resize(available_height);
	int current_y = y_ + 1 + available_height - 1; // start from bottom of available area
	int start_x = x_ + 1;
	int max_width = show_sidebar ? (divider_x - 1) : (width_ - 2);

	attron(COLOR_PAIR(get_background_color_pair()));

	// First, clear the content area
	for (int i = 1; i < height_ - 1; ++i) {
		move(y_ + i, x_ + 1);
		for (int j = 0; j < width_ - 2; ++j)
			addch(' ');
	}

	auto interactions = agent_->get_interactions();
	if (interactions.empty()) {
		attroff(COLOR_PAIR(get_background_color_pair()));
		return;
	}

	// Group interactions into turns and pre-assign backgrounds
	struct turn_group {
		std::vector<std::shared_ptr<agent_interaction>> items;
		int height{0};
		background_mode bg{background_mode::light_blue};
	};

	std::vector<turn_group> turns;
	int alternate_count = 0;

	for (const auto &inter : interactions) {
		if (turns.empty() || !inter->can_merge_with_previous(*turns.back().items.back())) {
			turn_group new_turn;
			new_turn.items.push_back(inter);

			if (inter->get_type() == interaction_type::system_message) {
				new_turn.bg = background_mode::white;
			} else {
				new_turn.bg = (alternate_count % 2 == 0) ? background_mode::light_blue : background_mode::cyan;
				alternate_count++;
			}
			turns.push_back(new_turn);
		} else {
			turns.back().items.push_back(inter);
		}
	}

	// Box drawing characters
	std::string top_left = "\xE2\x94\x8C";
	std::string horiz = "\xE2\x94\x80";
	std::string top_right = "\xE2\x94\x90";
	std::string vert = "\xE2\x94\x82";
	std::string bot_left = "\xE2\x94\x94";
	std::string bot_right = "\xE2\x94\x98";
	std::string sep_left = "\xE2\x94\x9C";
	std::string sep_right = "\xE2\x94\xA4";

	// Calculate heights for each turn
	int inner_width = max_width - 4;
	if (inner_width < 10)
		inner_width = 10;

	int total_turns_height = 0;
	for (auto &turn : turns) {
		turn.height = 2; // Top and bottom borders
		for (size_t i = 0; i < turn.items.size(); ++i) {
			if (i > 0) {
				bool skip_separator = false;
				auto prev_type = turn.items[i - 1]->get_type();
				auto curr_type = turn.items[i]->get_type();
				if (prev_type == interaction_type::tool_call && curr_type == interaction_type::tool_result) {
					skip_separator = true;
				} else if (prev_type == curr_type &&
					   (curr_type == interaction_type::tool_call || curr_type == interaction_type::tool_result)) {
					if (turn.items[i - 1]->get_grouping_key() == turn.items[i]->get_grouping_key()) {
						skip_separator = true;
					}
				}

				if (!skip_separator)
					turn.height++; // Separator line
			}
			auto lines = turn.items[i]->render(inner_width, turn.bg);
			turn.height += lines.size();
		}
		total_turns_height += turn.height;
	}

	if (last_width_ > 0 && width_ != last_width_) {
		// Skip adjustment during resize
	} else if (last_total_turns_height_ > 0 && total_turns_height > last_total_turns_height_) {
		int delta = total_turns_height - last_total_turns_height_;
		if (scroll_offset_ > 0) {
			scroll_offset_ += delta;
		}
	}
	last_total_turns_height_ = total_turns_height;
	last_width_ = width_;

	max_scroll_offset_ = (total_turns_height > available_height) ? (total_turns_height - available_height) : 0;
	scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll_offset_);

	int rendered_lines = 0;
	int skip_lines = scroll_offset_;

	// Render turns backwards from the bottom
	for (auto it = turns.rbegin(); it != turns.rend() && rendered_lines < available_height; ++it) {
		const auto &turn = *it;

		// If the whole turn is skipped, just decrement and move on
		if (skip_lines >= turn.height) {
			skip_lines -= turn.height;
			continue;
		}

		// Generate all lines for this turn's box
		std::vector<interaction_line> box_lines;
		int box_cp = get_color_pair(interaction_role::agent, turn.bg);

		// Top border
		interaction_line top_line;
		top_line.text = top_left;
		for (int i = 0; i < inner_width + 2; ++i)
			top_line.text += horiz;
		top_line.text += top_right;
		top_line.color_pair = box_cp;
		box_lines.push_back(top_line);

		for (size_t i = 0; i < turn.items.size(); ++i) {
			const auto &item = turn.items[i];
			if (i > 0) {
				bool skip_separator = false;
				auto prev_type = turn.items[i - 1]->get_type();
				auto curr_type = item->get_type();
				if (prev_type == interaction_type::tool_call && curr_type == interaction_type::tool_result) {
					skip_separator = true;
				} else if (prev_type == curr_type &&
					   (curr_type == interaction_type::tool_call || curr_type == interaction_type::tool_result)) {
					if (turn.items[i - 1]->get_grouping_key() == item->get_grouping_key()) {
						skip_separator = true;
					}
				}

				if (!skip_separator) {
					// Separator line
					interaction_line sep_line;
					sep_line.text = sep_left;
					if (item->needs_subpanel_header()) {
						std::string label = " " + item->get_subpanel_label() + " ";
						int label_len = utf8::display_width(label);
						int line_len = inner_width + 2;
						int left_pad = (line_len - label_len) / 2;
						int right_pad = line_len - label_len - left_pad;

						for (int j = 0; j < left_pad; ++j)
							sep_line.text += horiz;
						sep_line.text += label;
						for (int j = 0; j < right_pad; ++j)
							sep_line.text += horiz;
					} else {
						for (int j = 0; j < inner_width + 2; ++j)
							sep_line.text += horiz;
					}
					sep_line.text += sep_right;
					sep_line.color_pair = box_cp;
					box_lines.push_back(sep_line);
				}
			}

			auto content = item->render(inner_width, turn.bg);
			for (const auto &line : content) {
				interaction_line l = line;
				l.prefix = vert + " ";
				l.prefix_color_pair = box_cp;

				int content_len = 0;
				if (line.text.starts_with("__THUMBNAIL__:")) {
					content_len = inner_width;
				} else {
					content_len = utf8::display_width(line.text);
				}
				int pad_len = inner_width - content_len;
				if (pad_len < 0)
					pad_len = 0;

				if (l.color_pair == 3 && pad_len > 0) {
					l.text.append(pad_len, ' ');
					if (!l.char_color_pairs.empty()) {
						l.char_color_pairs.resize(l.char_color_pairs.size() + pad_len, 3);
					}
					pad_len = 0;
				}

				l.suffix = std::string(pad_len, ' ') + " " + vert;
				l.suffix_color_pair = box_cp;
				box_lines.push_back(l);
			}
		}

		// Bottom border
		interaction_line bot_line;
		bot_line.text = bot_left;
		for (int i = 0; i < inner_width + 2; ++i)
			bot_line.text += horiz;
		bot_line.text += bot_right;
		bot_line.color_pair = box_cp;
		box_lines.push_back(bot_line);

		// Render the visible lines of this turn box
		for (auto line_it = box_lines.rbegin(); line_it != box_lines.rend(); ++line_it) {
			if (skip_lines > 0) {
				skip_lines--;
				continue;
			}
			if (rendered_lines >= available_height)
				break;

			int viewport_idx = current_y - (y_ + 1);
			if (viewport_idx >= 0 && viewport_idx < available_height) {
				visible_lines_[viewport_idx] = *line_it;
			}

			int current_x = start_x;
			if (!line_it->prefix.empty()) {
				attron(COLOR_PAIR(line_it->prefix_color_pair));
				mvprintw(current_y, current_x, "%s", line_it->prefix.c_str());
				attroff(COLOR_PAIR(line_it->prefix_color_pair));
				current_x += utf8::display_width(line_it->prefix);
			}

			// Draw text with potential selection highlight
			if (line_it->custom_draw_fn) {
				line_it->custom_draw_fn(current_x, current_y, inner_width);
				current_x += inner_width;
			} else if (line_it->text.starts_with("__THUMBNAIL__:")) {
				attron(COLOR_PAIR(line_it->color_pair));
				mvprintw(current_y, current_x, "%s", std::string(inner_width, ' ').c_str());
				attroff(COLOR_PAIR(line_it->color_pair));

				std::string content = line_it->text.substr(14);
				size_t colon = content.find(':');
				if (colon != std::string::npos) {
					int r = std::stoi(content.substr(0, colon));
					std::string img_uri = content.substr(colon + 1);

					int thumb_w = 20;
					int thumb_h = 5;
					if (thumb_w > inner_width) {
						thumb_w = inner_width;
					}
					int left_pad = (inner_width - thumb_w) / 2;

					auto it = thumbnail_cache_.find(img_uri);
					if (it == thumbnail_cache_.end()) {
						std::string physical_path;
						if (img_uri.starts_with("images://")) {
							physical_path = images::image_manager::get_instance().resolve_uri(img_uri);
						} else {
							physical_path = images::image_manager::get_instance().resolve_uri("images://by-name/" + img_uri);
							if (physical_path.empty()) {
								physical_path = images::image_manager::get_instance().resolve_uri("images://" + img_uri);
							}
						}
						if (physical_path.empty() && std::filesystem::exists(img_uri)) {
							physical_path = img_uri;
						}

						cached_thumbnail cached;
						cached.width = thumb_w;
						cached.height = thumb_h;

						auto &registry = agentlib::filter_registry::get_instance();
						if (!physical_path.empty() && registry.has_filter("image_thumbnail")) {
							nlohmann::json input_json = {
								{"path", physical_path},
								{"width", thumb_w},
								{"height", thumb_h}
							};
							bool success = false;
							std::string result_str = registry.apply_filter("image_thumbnail", input_json.dump(), success);
							if (success) {
								try {
									nlohmann::json out = nlohmann::json::parse(result_str);
									int grid_w = out["width"].get<int>();
									int grid_h = out["height"].get<int>();
									cached.width = grid_w;
									cached.height = grid_h;
									auto raw_cells = out["cells"];
									
									for (const auto &item : raw_cells) {
										thumbnail_cell cell;
										cell.fg_r = item[0][0].get<int>();
										cell.fg_g = item[0][1].get<int>();
										cell.fg_b = item[0][2].get<int>();
										cell.bg_r = item[1][0].get<int>();
										cell.bg_g = item[1][1].get<int>();
										cell.bg_b = item[1][2].get<int>();
										cell.ch = item[2].get<std::string>();
										cached.cells.push_back(cell);
									}
								} catch (...) {}
							}
						}
						thumbnail_cache_[img_uri] = cached;
						it = thumbnail_cache_.find(img_uri);
					}

					const auto &cached = it->second;
					if (cached.cells.empty()) {
						std::string ph = " [No Preview] ";
						if (r == thumb_h / 2) {
							mvaddstr(current_y, current_x + left_pad + (thumb_w - (int)ph.length()) / 2, ph.c_str());
						}
					} else {
						int draw_x = current_x + left_pad;
						int grid_w = cached.width;

						for (int col = 0; col < thumb_w; ++col) {
							thumbnail_cell cell;
							size_t idx = r * grid_w + col;
							if (idx < cached.cells.size()) {
								cell = cached.cells[idx];
							}

							int fg_col = dynamic_colors::dynamic_get_color(cell.fg_r, cell.fg_g, cell.fg_b);
							int bg_col = dynamic_colors::dynamic_get_color(cell.bg_r, cell.bg_g, cell.bg_b);

							int pair_idx = dynamic_colors::dynamic_alloc_pair(fg_col, bg_col);

							wchar_t wc = L' ';
							if (cell.ch == " ") wc = L' ';
							else if (cell.ch == "▘") wc = 0x2598;
							else if (cell.ch == "▝") wc = 0x259D;
							else if (cell.ch == "▖") wc = 0x2596;
							else if (cell.ch == "▗") wc = 0x2597;
							else if (cell.ch == "▀") wc = 0x2580;
							else if (cell.ch == "▌") wc = 0x258C;
							else if (cell.ch == "▚") wc = 0x259A;

							cchar_t wch;
							wchar_t wstr[2] = { wc, 0 };
							setcchar(&wch, wstr, A_NORMAL, pair_idx, NULL);
							mvadd_wch(current_y, draw_x + col, &wch);
						}
					}
				}
				current_x += inner_width;
			} else {
				bool has_mouse_sel = (mouse_sel_start_line_ != -1 && mouse_sel_end_line_ != -1);
				int mouse_start_l = mouse_sel_start_line_;
				int mouse_start_c = mouse_sel_start_char_;
				int mouse_end_l = mouse_sel_end_line_;
				int mouse_end_c = mouse_sel_end_char_;
				if (has_mouse_sel) {
					if (mouse_start_l > mouse_end_l || (mouse_start_l == mouse_end_l && mouse_start_c > mouse_end_c)) {
						std::swap(mouse_start_l, mouse_end_l);
						std::swap(mouse_start_c, mouse_end_c);
					}
				}
				size_t byte_off = 0;
				std::string utf8_char;
				utf8_char.reserve(4);

				int char_idx = 0;
				int col_idx = 0;

				while (utf8::next_character(line_it->text, byte_off, utf8_char)) {
					bool in_selection = false;
					if (has_mouse_sel) {
						int line_viewport_y = current_y - (y_ + 1);
						if (line_viewport_y > mouse_start_l && line_viewport_y < mouse_end_l) {
							in_selection = true;
						} else if (line_viewport_y == mouse_start_l && line_viewport_y == mouse_end_l) {
							in_selection = (col_idx >= mouse_start_c && col_idx < mouse_end_c);
						} else if (line_viewport_y == mouse_start_l) {
							in_selection = (col_idx >= mouse_start_c);
						} else if (line_viewport_y == mouse_end_l) {
							in_selection = (col_idx < mouse_end_c);
						}
					}

					int cp = line_it->color_pair;
					int attr = 0;
					if (in_selection) {
						cp = 8;
					} else {
						if (!line_it->char_color_pairs.empty() && char_idx < static_cast<int>(line_it->char_color_pairs.size())) {
							cp = line_it->char_color_pairs[char_idx];
						}
						if (!line_it->char_attrs.empty() && char_idx < static_cast<int>(line_it->char_attrs.size())) {
							int logical_attr = line_it->char_attrs[char_idx];
							if (logical_attr & agentlib::ATTR_BOLD) attr |= A_BOLD;
							if (logical_attr & agentlib::ATTR_UNDERLINE) attr |= A_UNDERLINE;
						}
					}

					attron(COLOR_PAIR(cp) | attr);
					mvprintw(current_y, current_x, "%s", utf8_char.c_str());
					attroff(COLOR_PAIR(cp) | attr);
					
					int c_w = utf8::display_width(utf8_char);
					current_x += c_w;
					col_idx += c_w;
					char_idx++;
				}
			}

			if (!line_it->suffix.empty()) {
				attron(COLOR_PAIR(line_it->suffix_color_pair));
				mvprintw(current_y, current_x, "%s", line_it->suffix.c_str());
				attroff(COLOR_PAIR(line_it->suffix_color_pair));
			}

			current_y--;
			rendered_lines++;
		}
	}

	attroff(COLOR_PAIR(get_background_color_pair()));

	if (scroll_offset_ > 0) {
		int overlay_y = y_ + height_ - 6;
		int overlay_x = x_ + max_width - 12;
		attron(COLOR_PAIR(10));
		mvprintw(overlay_y, overlay_x, " \xE2\x96\xBC Follow ");
		attroff(COLOR_PAIR(10));
	}

	// 2. Draw the input box at the bottom
	if (input_box_) {
		// Draw separator line
		int input_box_y = y_ + height_ - 4;
		int separator_y = input_box_y - 1;

		int active_tokens = 0;
		int max_tokens = 250000;
		std::vector<compaction_segment> segments;
		if (agent_) {
			active_tokens = agent_->get_active_tokens();
			if (agent_->get_model()) {
				max_tokens = agent_->get_model()->get_max_context_tokens();
			}
			segments = agent_->get_compaction_segments();
		}

		int total_uncompacted = 0;
		for (const auto &seg : segments) {
			total_uncompacted += seg.uncompacted_tokens;
		}

		if (total_uncompacted <= 0) {
			attrset(COLOR_PAIR(get_background_color_pair()));
			for (int i = 0; i < max_width; ++i) {
				mvaddch(separator_y, start_x + i, ACS_HLINE);
			}
		} else {
			struct col_info {
				std::string utf8_char{"\xE2\x94\x80"};
				int color_pair{3};
			};
			std::vector<col_info> cols(max_width);
			for (int i = 0; i < max_width; ++i) {
				cols[i].color_pair = get_background_color_pair();
			}

			int accumulated_chars = 0;
			for (size_t idx = 0; idx < segments.size(); ++idx) {
				const auto &seg = segments[idx];
				int seg_width = static_cast<int>(
				    std::round(static_cast<double>(seg.uncompacted_tokens) / total_uncompacted * max_width));
				int start_col = accumulated_chars;
				accumulated_chars += seg_width;
				int end_col = (idx == segments.size() - 1) ? max_width : accumulated_chars;
				if (end_col > max_width)
					end_col = max_width;
				if (start_col > max_width)
					start_col = max_width;

				std::string utf8_char = "\xE2\x94\x80";
				int cp = get_background_color_pair();

				if (seg.current_level == 0) {
					utf8_char = "\xE2\x96\x88"; // █
					cp = 30;
				} else if (seg.current_level == 1) {
					utf8_char = "\xE2\x96\x92"; // ▒
					cp = 21;
				} else if (seg.current_level == 2) {
					utf8_char = "\xE2\x96\x91"; // ░
					cp = 32;
				} else if (seg.current_level == 99) {
					utf8_char = "\xC2\xB7"; // ·
					cp = 4;
				}

				for (int col = start_col; col < end_col; ++col) {
					cols[col] = {utf8_char, cp};
				}
			}

			auto format_tokens = [](int tokens) -> std::string {
				if (tokens >= 1000) {
					double k = tokens / 1000.0;
					if (k >= 100.0) {
						return std::to_string(static_cast<int>(std::round(k))) + "k";
					} else {
						std::stringstream ss;
						ss << std::fixed << std::setprecision(1) << k << "k";
						return ss.str();
					}
				}
				return std::to_string(tokens);
			};

			std::string budget_text = " [ " + format_tokens(active_tokens) + " / " + format_tokens(max_tokens) + " ] ";
			int budget_len = budget_text.length();
			int budget_start_col = max_width - budget_len - 2;
			if (budget_start_col < 0) {
				budget_start_col = 0;
			}

			for (int col = 0; col < max_width; ++col) {
				if (col >= budget_start_col && col < budget_start_col + budget_len) {
					attrset(COLOR_PAIR(5));
					mvprintw(separator_y, start_x + col, "%c", budget_text[col - budget_start_col]);
				} else {
					attrset(COLOR_PAIR(cols[col].color_pair));
					mvprintw(separator_y, start_x + col, "%s", cols[col].utf8_char.c_str());
				}
			}
		}

		input_box_->set_bounds(start_x, input_box_y, max_width, 3);
		input_box_->set_focus(is_active() && (!show_sidebar || sidebar_focus_ == sidebar_focus::input));
		// Components must always be drawn using their computed absolute screen coordinates
		// to prevent clobbering other parts of the interface (e.g. the top menu or borders).
		input_box_->draw(input_box_->x(), input_box_->y());
	}

	// 3. Draw sidebar if active
	if (show_sidebar) {
		int border_pair = is_active() ? 5 : 38;

		// Draw vertical divider
		attrset(COLOR_PAIR(border_pair));
		for (int i = 1; i < height_ - 1; ++i) {
			if (i == 2) {
				mvaddstr(y_ + i, abs_divider_x, "►");
			} else {
				mvaddstr(y_ + i, abs_divider_x, "│");
			}
		}
		attroff(COLOR_PAIR(border_pair));

		// Draw Subagents listbox
		if (has_subagents) {
			std::vector<std::string> subagent_strings;
			for (const auto &sub : get_active_subagents()) {
				std::string sub_status = agent_status_to_string(sub->get_status(), sub->get_current_tool());
				if (sub->get_status() == agent_status::waiting) {
					sub_status = std::format("Waiting ({})", sub->get_waiting_on_id());
				}
				subagent_strings.push_back(std::format("{} [{}]", sub->get_name(), sub_status));
			}
			int sub_y = y_ + 1;
			int sub_h = height_ - 2;
			subagents_list_->set_bounds(abs_divider_x + 1, sub_y, width_ - 2 - divider_x, sub_h);
			subagents_list_->set_items(subagent_strings);
			subagents_list_->set_focus(is_active() && sidebar_focus_ == sidebar_focus::subagents);
			subagents_list_->draw(subagents_list_->x(), subagents_list_->y());
		}
	}
}

int agent_window::get_history_viewport_height() const
{
	int input_box_height = 3;
	int separator_height = 1;
	int reserved_bottom = input_box_height + separator_height;
	return height_ - 2 - reserved_bottom;
}

std::string agent_window::get_mouse_selected_text() const
{
	if (mouse_sel_start_line_ == -1 || mouse_sel_end_line_ == -1) {
		return "";
	}

	int start_l = mouse_sel_start_line_;
	int start_c = mouse_sel_start_char_;
	int end_l = mouse_sel_end_line_;
	int end_c = mouse_sel_end_char_;

	// Swap if start is after end
	if (start_l > end_l || (start_l == end_l && start_c > end_c)) {
		std::swap(start_l, end_l);
		std::swap(start_c, end_c);
	}

	std::string result;
	for (int l = start_l; l <= end_l; ++l) {
		if (l < 0 || l >= static_cast<int>(visible_lines_.size()))
			continue;
		const auto &line = visible_lines_[l];
		int len = utf8::display_width(line.text);

		int from_c = (l == start_l) ? start_c : 0;
		int to_c = (l == end_l) ? end_c : len;

		from_c = std::clamp(from_c, 0, len);
		to_c = std::clamp(to_c, 0, len);

		if (from_c < to_c) {
			size_t from_byte = 0, to_byte = 0, curr_byte = 0;
			int curr_col = 0;
			std::string utf8_char;
			while (utf8::next_character(line.text, curr_byte, utf8_char)) {
				int w = utf8::display_width(utf8_char);
				if (curr_col < from_c) from_byte = curr_byte;
				if (curr_col < to_c) to_byte = curr_byte;
				curr_col += w;
			}
			if (from_byte < to_byte) {
				result += line.text.substr(from_byte, to_byte - from_byte);
			}
		}

		if (l < end_l) {
			result += "\n";
		}
	}
	return result;
}

void agent_window::draw_border() const
{
	window::draw_border();

	bool has_subagents = !get_active_subagents().empty();
	bool show_sidebar = sidebar_expanded_ && has_subagents;

	if (show_sidebar) {
		int sidebar_w = (width_ * 30) / 100;
		sidebar_w = std::max(15, std::min(sidebar_w, width_ - 20));
		int divider_x = width_ - 1 - sidebar_w;
		int abs_divider_x = x_ + divider_x;

		int border_pair = is_active() ? 5 : 38;
		attrset(COLOR_PAIR(border_pair));

		// Top junction
		mvaddstr(y_, abs_divider_x, "╤");

		// Bottom junction
		mvaddstr(y_ + height_ - 1, abs_divider_x, "╧");

		attroff(COLOR_PAIR(border_pair));
	} else if (sidebar_expanded_ && has_subagents) {
		// Draw expand button on right border when collapsed
		int border_pair = is_active() ? 5 : 38;
		attrset(COLOR_PAIR(border_pair));
		mvaddstr(y_ + 2, x_ + width_ - 1, "◄");
		attroff(COLOR_PAIR(border_pair));
	}
}

void agent_window::set_cursor_position() const
{
	if (is_active()) {
		bool has_subagents = !get_active_subagents().empty();
		bool show_sidebar = sidebar_expanded_ && has_subagents;

		if (show_sidebar && sidebar_focus_ == sidebar_focus::subagents && subagents_list_) {
			// Align the virtual cursor coordinates with the component's absolute bounds.
			subagents_list_->set_cursor_position(subagents_list_->x(), subagents_list_->y());
		} else if (input_box_) {
			// Draw input box at its absolute location, which also sets the cursor position.
			input_box_->draw(input_box_->x(), input_box_->y());
		}
	}
}

bool agent_window::update_viewport() const
{
	bool changed = (scroll_offset_ != last_scroll_offset_);
	last_scroll_offset_ = scroll_offset_;
	return changed;
}

std::vector<std::shared_ptr<agentlib::ai_agent>> agent_window::get_active_subagents() const
{
	std::vector<std::shared_ptr<agentlib::ai_agent>> active;
	if (!agent_) {
		return active;
	}
	// Filter subagents to only retrieve those that are not in the terminal dead state,
	// ensuring dead subagents are hidden from the sidebar lists.
	for (const auto &sub : agent_->get_subagents()) {
		if (sub->get_status() != agentlib::agent_status::dead) {
			active.push_back(sub);
		}
	}
	return active;
}

int agent_window::get_scroll_total_lines() const
{
	return max_scroll_offset_ + get_scroll_content_height();
}

int agent_window::get_scroll_top_line() const
{
	return max_scroll_offset_ - scroll_offset_;
}

int agent_window::get_scroll_content_height() const
{
	return std::max(1, height_ - 4);
}
