#include "ui/agent_window.h"
#include <algorithm>
#include <ncurses.h>
#include <nlohmann/json.hpp>
#include "agentlib/httplib_transport.h"
#include "agentlib/skill_manager.h"
#include "config_manager.h"
#include "fs_utils.h"
#include "git_manager.h"
#include "markdown_utils.h"
#include "project_manager.h"

using namespace agentlib;

agent_window::agent_window(int id, int x, int y, int width, int height, std::shared_ptr<agentlib::ai_model> model,
			   event_queue &global_queue, agentlib::document_provider *doc_provider)
    : window(id, x, y, width, height, "Agent Chat")
{
	agent_ = ai_agent::create(id, "Agent", std::move(model), &global_queue, doc_provider);

	std::string system_prompt =
	    "You are an expert AI programming assistant.\n"
	    "Your goal is to help the user navigate, understand, and safely modify this codebase.\n"
	    "You have access to a suite of highly optimized, secure, and syntax-aware tools.\n"
	    "STRONGLY PREFER using built-in tools (e.g., fs_read_lines, fs_replace_lines, sqlite_perform, git_status) over the generic "
	    "`run_shell_command` tool.\n"
	    "Built-in tools are faster, automatically format their output for you, and do not require the user to manually approve a "
	    "security dialog for every action.\n"
	    "Only use `run_shell_command` when absolutely necessary for tasks that cannot be accomplished with built-in tools.\n\n"
	    "*** CRITICAL DIRECTIVE: MEMORY MANAGEMENT ***\n"
	    "Your context window is strictly limited. To prevent crashing and save costs, you MUST manually drop memory anchors.\n"
	    "If the user says 'let's move on', 'next task', or introduces a completely unrelated topic or goal, YOU MUST immediately "
	    "call the `agent_mark_milestone` tool BEFORE starting the new work. This allows the system to compress old history.\n"
	    "Do NOT wait to be asked. Proactively call it whenever a logical chapter of work concludes.";

	system_prompt += project_manager::get_instance().get_project_knowledge_prompt();

	agent_->inject_context("system", system_prompt);
	
	// Load the active state from the previous session if it exists.
	// This inherently gives us Cross-Session Persistence as per the design doc.
	if (agent_->load_active_state()) {
		agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Agent state restored from previous session."));
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

		if (trimmed_text == "/quit") {
			input_box_->set_buffer(""); // Clear the box
			editor_event ev;
			ev.type = event_type::close_window;
			ev.key_code = id; // Pass the window ID so the dispatcher knows which one to close
			if (agent_->get_global_queue()) {
				agent_->get_global_queue()->push(ev);
			} else {
				get_queue().push(ev);
			}
			return;
		}

		if (trimmed_text == "/model") {
			input_box_->set_buffer(""); // Clear the box
			editor_event ev;
			ev.type = event_type::agent_switch_model;
			ev.key_code = id; // Pass the window ID
			if (agent_->get_global_queue()) {
				agent_->get_global_queue()->push(ev);
			} else {
				get_queue().push(ev);
			}
			return;
		}

		if (trimmed_text.starts_with("/save")) {
			input_box_->set_buffer(""); // Clear the box
			std::string tmp_dir = fs_utils::get_project_tmp_dir();
			std::string filepath = tmp_dir + "/agent_chat_" + std::to_string(id) + ".json";
			agent_->save_conversation(filepath);

			// Show a system message that it was saved
			agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Conversation saved to: " + filepath));
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		if (trimmed_text.starts_with("/stats")) {
			input_box_->set_buffer(""); // Clear the box
			auto stats = agent_->get_stats();
			std::string stats_str = "Agent Statistics:\n";
			if (stats.empty()) {
				stats_str += "  (No stats recorded yet)";
			} else {
				for (const auto& [key, value] : stats) {
					stats_str += "  " + key + ": " + std::to_string(value) + "\n";
				}
			}
			agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>(stats_str));
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		if (trimmed_text.starts_with("/help")) {
			input_box_->set_buffer("");
			std::string help_str = "Available Commands:\n"
			                       "  /help             - Show this help message\n"
			                       "  /quit             - Close the agent window\n"
			                       "  /save             - Save the active context to disk manually\n"
			                       "  /stats            - Show compaction and performance statistics\n"
			                       "  /memory           - List all paged-out history archives\n"
			                       "  /milestone [text] - Drop a semantic anchor and compress history manually\n"
			                       "  /pageout <N>      - Page out the first N turns of the active session\n"
			                       "  /pagein <id>      - Restore an archived milestone into active memory\n"
			                       "  /model            - Switch the AI model for this agent";
			agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>(help_str));
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		if (trimmed_text.starts_with("/memory")) {
			input_box_->set_buffer(""); // Clear the box
			std::string mem_index = agent_->get_memory_index();
			agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>(mem_index));
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		if (trimmed_text.starts_with("/milestone")) {
			input_box_->set_buffer("");
			std::string title = "User Milestone";
			if (trimmed_text.length() > 11) {
				title = trimmed_text.substr(11);
			}
			
			// We trigger a proactive context compaction. It scans backwards and compresses
			// everything up to the previous milestone.
			agent_->page_out_prior_context("", false, title, "User manually triggered milestone: " + title, {"manual-milestone"});
			agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Milestone manually recorded. History compressed."));
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		if (trimmed_text.starts_with("/pageout ")) {
			input_box_->set_buffer("");
			try {
				int n = std::stoi(trimmed_text.substr(9));
				agent_->page_out_context(1, n + 1, "Manual Pageout", "User manually triggered /pageout " + std::to_string(n), {});
				agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Successfully paged out " + std::to_string(n) + " turns."));
			} catch (...) {
				agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Usage: /pageout <number_of_turns>"));
			}
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		if (trimmed_text.starts_with("/pagein ")) {
			input_box_->set_buffer("");
			std::string milestone_id = trimmed_text.substr(8);
			if (agent_->page_in_context(milestone_id)) {
				agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Successfully paged in context: " + milestone_id));
			} else {
				agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Failed to page in context: " + milestone_id));
			}
			scroll_offset_ = 0;
			invalidate();
			return;
		}
		
		agent_->submit_prompt(text);
		input_box_->set_buffer(""); // Clear the box
		scroll_offset_ = 0;
		invalidate();
	});

	input_box_->set_on_change([this](const std::string &text) {
		if (text.starts_with("/")) {
			editor_event status_ev;
			status_ev.type = event_type::set_transient_status;
			status_ev.payload = "Commands: /help /quit /save /stats /memory /milestone /pageout /pagein /model";
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
			if (agent_->get_global_queue()) {
				agent_->get_global_queue()->push(status_ev);
			} else {
				get_queue().push(status_ev);
			}
		}
	});

	// List available skills at startup for the user
	auto &skills = skill_manager::get_instance().get_skills();
	if (!skills.empty()) {
		std::string skills_text = "Available Skills:\n";
		for (const auto &s : skills) {
			skills_text += "- " + s.name + "\n";
		}
		agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>(skills_text));
	}
}

agent_window::agent_window(int id, int x, int y, int width, int height, std::shared_ptr<agentlib::ai_agent> existing_agent)
    : window(id, x, y, width, height, existing_agent->get_name()), agent_(std::move(existing_agent))
{
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

		if (trimmed_text == "/quit") {
			input_box_->set_buffer(""); // Clear the box
			editor_event ev;
			ev.type = event_type::close_window;
			ev.key_code = id; // Pass the window ID so the dispatcher knows which one to close
			if (agent_->get_global_queue()) {
				agent_->get_global_queue()->push(ev);
			} else {
				get_queue().push(ev);
			}
			return;
		}

		if (trimmed_text == "/model") {
			input_box_->set_buffer(""); // Clear the box
			editor_event ev;
			ev.type = event_type::agent_switch_model;
			ev.key_code = id; // Pass the window ID
			if (agent_->get_global_queue()) {
				agent_->get_global_queue()->push(ev);
			} else {
				get_queue().push(ev);
			}
			return;
		}

		if (trimmed_text.starts_with("/save")) {
			input_box_->set_buffer(""); // Clear the box
			std::string tmp_dir = fs_utils::get_project_tmp_dir();
			std::string filepath = tmp_dir + "/agent_chat_" + std::to_string(id) + ".json";
			agent_->save_conversation(filepath);

			// Show a system message that it was saved
			agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Conversation saved to: " + filepath));
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		if (trimmed_text.starts_with("/stats")) {
			input_box_->set_buffer(""); // Clear the box
			auto stats = agent_->get_stats();
			std::string stats_str = "Agent Statistics:\n";
			if (stats.empty()) {
				stats_str += "  (No stats recorded yet)";
			} else {
				for (const auto& [key, value] : stats) {
					stats_str += "  " + key + ": " + std::to_string(value) + "\n";
				}
			}
			agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>(stats_str));
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		if (trimmed_text.starts_with("/help")) {
			input_box_->set_buffer("");
			std::string help_str = "Available Commands:\n"
			                       "  /help             - Show this help message\n"
			                       "  /quit             - Close the agent window\n"
			                       "  /save             - Save the active context to disk manually\n"
			                       "  /stats            - Show compaction and performance statistics\n"
			                       "  /memory           - List all paged-out history archives\n"
			                       "  /milestone [text] - Drop a semantic anchor and compress history manually\n"
			                       "  /pageout <N>      - Page out the first N turns of the active session\n"
			                       "  /pagein <id>      - Restore an archived milestone into active memory\n"
			                       "  /model            - Switch the AI model for this agent";
			agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>(help_str));
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		if (trimmed_text.starts_with("/memory")) {
			input_box_->set_buffer(""); // Clear the box
			std::string mem_index = agent_->get_memory_index();
			agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>(mem_index));
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		if (trimmed_text.starts_with("/milestone")) {
			input_box_->set_buffer("");
			std::string title = "User Milestone";
			if (trimmed_text.length() > 11) {
				title = trimmed_text.substr(11);
			}
			
			// We trigger a proactive context compaction. It scans backwards and compresses
			// everything up to the previous milestone.
			agent_->page_out_prior_context("", false, title, "User manually triggered milestone: " + title, {"manual-milestone"});
			agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Milestone manually recorded. History compressed."));
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		if (trimmed_text.starts_with("/pageout ")) {
			input_box_->set_buffer("");
			try {
				int n = std::stoi(trimmed_text.substr(9));
				agent_->page_out_context(1, n + 1, "Manual Pageout", "User manually triggered /pageout " + std::to_string(n), {});
				agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Successfully paged out " + std::to_string(n) + " turns."));
			} catch (...) {
				agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Usage: /pageout <number_of_turns>"));
			}
			scroll_offset_ = 0;
			invalidate();
			return;
		}

		if (trimmed_text.starts_with("/pagein ")) {
			input_box_->set_buffer("");
			std::string milestone_id = trimmed_text.substr(8);
			if (agent_->page_in_context(milestone_id)) {
				agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Successfully paged in context: " + milestone_id));
			} else {
				agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>("Failed to page in context: " + milestone_id));
			}
			scroll_offset_ = 0;
			invalidate();
			return;
		}
		
		agent_->submit_prompt(text);
		input_box_->set_buffer(""); // Clear the box
		scroll_offset_ = 0;
		invalidate();
	});

	input_box_->set_on_change([this](const std::string &text) {
		if (text.starts_with("/")) {
			editor_event status_ev;
			status_ev.type = event_type::set_transient_status;
			status_ev.payload = "Commands: /help /quit /save /stats /memory /milestone /pageout /pagein /model";
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
			if (agent_->get_global_queue()) {
				agent_->get_global_queue()->push(status_ev);
			} else {
				get_queue().push(status_ev);
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
		if (ev->type == event_type::key_press) {
			int key = ev->key_code;

			if ((agent_->get_status() != agent_status::idle)) {
				if (key == 27) {
					agent_->cancel_current_task();
					needs_render = true;
				}
				continue; // Ignore input while waiting
			}

			if (is_active() && input_box_ && input_box_->handle_event(*ev, 0, 0)) {
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
			} else if (key == 21) { // Ctrl-U (Page up)
				scroll_offset_ += get_content_height() - 3;
				needs_render = true;
			} else if (key == 22) { // Ctrl-V (Page down)
				scroll_offset_ -= get_content_height() - 3;
				if (scroll_offset_ < 0)
					scroll_offset_ = 0;
				needs_render = true;
			}
		} else if (ev->type == event_type::paste) {
			if ((agent_->get_status() != agent_status::idle)) {
				continue; // Ignore input while waiting
			}
			if (is_active() && input_box_ && input_box_->handle_event(*ev, 0, 0)) {
				needs_render = true;
			}
		}
	}

	if (needs_render)
		invalidate();
	return needs_render;
}

void agent_window::on_agent_update()
{
	scroll_offset_ = 0; // Snap to bottom
	invalidate();
}

void agent_window::draw_content() const
{
	// 1. Draw the chat history in the upper portion
	int input_box_height = 3;
	int separator_height = 1;
	int reserved_bottom = input_box_height + separator_height;
	int available_height = height_ - 2 - reserved_bottom;
	int current_y = y_ + 1 + available_height - 1; // start from bottom of available area
	int start_x = x_ + 1;
	int max_width = width_ - 2;

	attron(COLOR_PAIR(get_background_color_pair()));

	// First, clear the content area
	for (int i = 1; i <= available_height; ++i) {
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

	for (auto &turn : turns) {
		turn.height = 2; // Top and bottom borders
		for (size_t i = 0; i < turn.items.size(); ++i) {
			if (i > 0) {
				bool skip_separator = false;
				auto prev_type = turn.items[i-1]->get_type();
				auto curr_type = turn.items[i]->get_type();
				if (prev_type == interaction_type::tool_call && curr_type == interaction_type::tool_result) {
					skip_separator = true;
				} else if (prev_type == curr_type && (curr_type == interaction_type::tool_call || curr_type == interaction_type::tool_result)) {
					if (turn.items[i-1]->get_grouping_key() == turn.items[i]->get_grouping_key()) {
						skip_separator = true;
					}
				}

				if (!skip_separator)
					turn.height++; // Separator line
			}
			auto lines = turn.items[i]->render(inner_width, turn.bg);
			turn.height += lines.size();
		}
	}

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
		int box_cp = get_color_pair(turn.items.front()->get_role(), turn.bg);

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
				auto prev_type = turn.items[i-1]->get_type();
				auto curr_type = item->get_type();
				if (prev_type == interaction_type::tool_call && curr_type == interaction_type::tool_result) {
					skip_separator = true;
				} else if (prev_type == curr_type && (curr_type == interaction_type::tool_call || curr_type == interaction_type::tool_result)) {
					if (turn.items[i-1]->get_grouping_key() == item->get_grouping_key()) {
						skip_separator = true;
					}
				}

				if (!skip_separator) {
					// Separator line
					interaction_line sep_line;
					sep_line.text = sep_left;
					if (item->needs_subpanel_header()) {
						std::string label = " " + item->get_subpanel_label() + " ";
						int label_len = markdown_utils::utf8_length(label);
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

				int content_len = markdown_utils::utf8_length(line.text);
				int pad_len = inner_width - content_len;
				if (pad_len < 0)
					pad_len = 0;

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

			int current_x = start_x;
			if (!line_it->prefix.empty()) {
				attron(COLOR_PAIR(line_it->prefix_color_pair));
				mvprintw(current_y, current_x, "%s", line_it->prefix.c_str());
				attroff(COLOR_PAIR(line_it->prefix_color_pair));
				current_x += markdown_utils::utf8_length(line_it->prefix);
			}

			attron(COLOR_PAIR(line_it->color_pair));
			mvprintw(current_y, current_x, "%s", line_it->text.c_str());
			attroff(COLOR_PAIR(line_it->color_pair));
			current_x += markdown_utils::utf8_length(line_it->text);

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

	// 2. Draw the input box at the bottom
	if (input_box_) {
		// Draw separator line
		int input_box_y = y_ + height_ - 4;
		int separator_y = input_box_y - 1;

		attrset(COLOR_PAIR(get_background_color_pair()));
		for (int i = 0; i < max_width; ++i) {
			mvaddch(separator_y, start_x + i, ACS_HLINE);
		}

		input_box_->set_bounds(start_x, input_box_y, max_width, 3);
		input_box_->set_focus(is_active());
		input_box_->draw(0, 0);
	}
}

void agent_window::set_cursor_position() const
{
	if (is_active() && input_box_) {
		input_box_->draw(0, 0);
	}
}
