#include "ui/agent_window.h"
#include <algorithm>
#include <ncurses.h>
#include <nlohmann/json.hpp>
#include "agentlib/httplib_transport.h"
#include "agentlib/skill_manager.h"
#include "config_manager.h"
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
	    "Only use `run_shell_command` when absolutely necessary for tasks that cannot be accomplished with built-in tools.";

	system_prompt += project_manager::get_instance().get_project_knowledge_prompt();

	agent_->inject_context("system", system_prompt);

	set_background_color_pair(17); // Use cyan background to differentiate from normal editors

	input_box_ = std::make_unique<ui_multiline_edit>("input", 0, 0, width_ - 2, 3, [this](const std::string &text) {
		agent_->submit_prompt(text);
		scroll_offset_ = 0;
		invalidate();
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

	input_box_ = std::make_unique<ui_multiline_edit>("input", 0, 0, width_ - 2, 3, [this](const std::string &text) {
		agent_->submit_prompt(text);
		scroll_offset_ = 0;
		invalidate();
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

	// Group interactions into turns
	struct turn_group {
		std::vector<std::shared_ptr<agent_interaction>> items;
		int height{0};
		int color_pair{1}; // Default white
	};

	std::vector<turn_group> turns;
	for (const auto &inter : interactions) {
		if (turns.empty() || !inter->can_merge_with_previous(*turns.back().items.back())) {
			turn_group new_turn;
			new_turn.items.push_back(inter);
			if (dynamic_cast<const interaction_system_message *>(inter.get())) {
				new_turn.color_pair = 2; // Red on White/Gray
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
			if (i > 0)
				turn.height++; // Separator line
			auto lines = turn.items[i]->render(inner_width);
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

		// Top border
		interaction_line top_line;
		top_line.text = top_left;
		for (int i = 0; i < inner_width + 2; ++i)
			top_line.text += horiz;
		top_line.text += top_right;
		top_line.color_pair = turn.color_pair;
		box_lines.push_back(top_line);

		for (size_t i = 0; i < turn.items.size(); ++i) {
			const auto &item = turn.items[i];
			if (i > 0) {
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
				sep_line.color_pair = turn.color_pair;
				box_lines.push_back(sep_line);
			}

			auto content = item->render(inner_width);
			for (const auto &line : content) {
				interaction_line l = line;
				l.prefix = vert + " ";
				l.prefix_color_pair = turn.color_pair;

				int content_len = markdown_utils::utf8_length(line.text);
				int pad_len = inner_width - content_len;
				if (pad_len < 0)
					pad_len = 0;

				l.suffix = std::string(pad_len, ' ') + " " + vert;
				l.suffix_color_pair = turn.color_pair;
				box_lines.push_back(l);
			}
		}

		// Bottom border
		interaction_line bot_line;
		bot_line.text = bot_left;
		for (int i = 0; i < inner_width + 2; ++i)
			bot_line.text += horiz;
		bot_line.text += bot_right;
		bot_line.color_pair = turn.color_pair;
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
