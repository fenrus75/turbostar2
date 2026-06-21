#include "ui/components/ui_agent_tile.h"
#include <ncurses.h>
#include "event_logger.h"
#include "ui/components/ui_durmovie.h"
#include "utf8.h"

// Helper to format and pad/truncate a string to exactly max_len display width
static std::string format_line(const std::string &content, size_t max_len)
{
	size_t disp_w = utf8::display_width(content);
	if (disp_w > max_len) {
		// Truncate safely at character boundaries, leaving room for ".."
		size_t byte_offset = 0;
		std::string result;
		std::string glyph;
		size_t current_w = 0;
		while (current_w + 2 < max_len && utf8::next_character(content, byte_offset, glyph)) {
			result += glyph;
			current_w += utf8::display_width(glyph);
		}
		result += "..";

		// Fill remaining space if truncation ended slightly short due to double-width characters
		disp_w = utf8::display_width(result);
		if (disp_w < max_len) {
			result.append(max_len - disp_w, ' ');
		}
		return result;
	} else {
		std::string result = content;
		result.append(max_len - disp_w, ' ');
		return result;
	}
}

ui_agent_tile::ui_agent_tile(std::string name, int x, int y, std::shared_ptr<agentlib::ai_agent> agent)
    : ui_container(std::move(name), x, y, 22, 12), agent_(std::move(agent)), last_tokens_rx_(agent_ ? agent_->get_tokens_rx() : 0),
      last_frame_time_(std::chrono::steady_clock::now()),
      last_token_increase_time_(std::chrono::steady_clock::now() - std::chrono::seconds(2))
{
	// Create the movie widget and add it as a child at (1, 1)
	auto movie = std::make_unique<ui_durmovie>("durmovie", 1, 1, 20, 8);
	durmovie_ = movie.get();
	add_child(std::move(movie));

	// Initial layout setup
	flow();
}

void ui_agent_tile::draw(int abs_x, int abs_y) const
{
	// Draw standard border around tile
	ui_utils::border_style style = ui_utils::border_style::single;
	int color_pair = 38; // Dim/Dark Gray on Blue
	if (has_focus()) {
		style = ui_utils::border_style::double_line;
		color_pair = 22; // Bright Cyan on Blue
	}
	ui_utils::draw_border(abs_x, abs_y, width(), height(), style, color_pair);

	// 1. Draw the child durmovie icon at top 8 lines (offset by 1, 1 inside border)
	ui_container::draw(abs_x, abs_y);

	if (!agent_) {
		return;
	}

	auto now = std::chrono::steady_clock::now();
	auto time_since_activity = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_token_increase_time_).count();
	bool is_currently_streaming = (time_since_activity < 1000);

	// 2. Render tool call line (index 9)
	std::string tool_name = agent_->get_current_tool();
	std::string tool_text;
	if (is_currently_streaming) {
		static const char *anim_frames[] = {"X███████", "█X██████", "██X█████", "███X████",
						    "████X███", "█████X██", "██████X█", "███████X"};
		std::string bar = anim_frames[current_anim_frame_];
		if (!tool_name.empty()) {
			std::string truncated_tool = tool_name;
			if (truncated_tool.length() > 6) {
				truncated_tool = truncated_tool.substr(0, 5) + "..";
			}
			tool_text = "T: " + truncated_tool + " [" + bar + "]";
		} else {
			tool_text = "Streaming [" + bar + "]";
		}
	} else {
		if (!tool_name.empty()) {
			tool_text = "Tool: " + tool_name;
		} else {
			tool_text = "T: none";
		}
	}

	std::string formatted_tool = format_line(tool_text, 20);
	int tool_pair = 5; // Default: Bright White on Blue
	if (is_currently_streaming) {
		tool_pair = 22; // Bright Cyan on Blue
	}
	attron(COLOR_PAIR(tool_pair));
	mvaddstr(abs_y + 9, abs_x + 1, formatted_tool.c_str());
	attroff(COLOR_PAIR(tool_pair));

	// 3. Render status line (index 10)
	auto status = agent_->get_status();
	std::string status_str = agent_status_to_string(status, tool_name);
	std::string status_text;
	bool show_spinner = (status == agentlib::agent_status::thinking || status == agentlib::agent_status::tool_execution);
	if (show_spinner) {
		static const std::string spinner_chars[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
		status_text = "S: " + spinner_chars[spinner_frame_ % 10] + " " + status_str;
	} else {
		status_text = "S: " + status_str;
	}
	std::string formatted_status = format_line(status_text, 20);

	// Map status to aesthetic colors
	int status_pair = 3; // Default: Yellow on Blue
	switch (status) {
		case agentlib::agent_status::thinking:
			status_pair = 30; // Bright Green on Blue
			break;
		case agentlib::agent_status::tool_execution:
			status_pair = 22; // Bright Cyan on Blue
			break;
		case agentlib::agent_status::error:
			status_pair = 31; // Bright Red on Blue
			break;
		case agentlib::agent_status::waiting:
			status_pair = 3; // Yellow on Blue
			break;
		case agentlib::agent_status::dead:
			status_pair = 38; // Dim/Dark Gray on Blue
			break;
		default:
			status_pair = 38; // Dim/Dark Gray on Blue
			break;
	}

	attron(COLOR_PAIR(status_pair));
	mvaddstr(abs_y + 10, abs_x + 1, formatted_status.c_str());
	attroff(COLOR_PAIR(status_pair));
}

bool ui_agent_tile::update_animation()
{
	// Propagate animation update to child widgets (e.g. ui_durmovie)
	bool child_changed = ui_container::update_animation();
	bool local_changed = false;

	if (!agent_) {
		return child_changed;
	}

	auto now = std::chrono::steady_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_time_).count();

	uint64_t current_tokens = agent_->get_tokens_rx();
	bool tokens_increased = (current_tokens > last_tokens_rx_);

	// Frame increment rule: 250ms elapsed AND tokens have increased since last increment
	if (ms >= 250) {
		if (tokens_increased) {
			current_anim_frame_ = (current_anim_frame_ + 1) % 8;
			spinner_frame_ = (spinner_frame_ + 1) % 10;
			last_tokens_rx_ = current_tokens;
			last_token_increase_time_ = now;
			local_changed = true;
		}
		last_frame_time_ = now;
	}

	// Sync durmovie active state with active token streaming
	auto time_since_activity = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_token_increase_time_).count();
	if (time_since_activity < 1000) {
		if (durmovie_->get_state() != durmovie_state::active) {
			durmovie_->set_state(durmovie_state::active);
			local_changed = true;
		}
	} else {
		if (durmovie_->get_state() != durmovie_state::idle) {
			durmovie_->set_state(durmovie_state::idle);
			local_changed = true;
		}
	}

	return child_changed || local_changed;
}

bool ui_agent_tile::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	return ui_container::handle_event(ev, abs_x, abs_y);
}
