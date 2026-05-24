#include "reasoning.h"
#include "../../markdown_utils.h"

namespace agentlib
{

std::vector<interaction_line> interaction_reasoning::format_lines(int width, background_mode bg) const
{
	int color = get_color_pair(interaction_role::thinking, bg);

	if (get_age() > 5) {
		// Return a single line separator spanning the width
		std::vector<interaction_line> lines;
		std::string horiz = "\xE2\x94\x80"; // ─
		std::string title = " Agent Reasoning ";
		int title_len = markdown_utils::utf8_length(title);

		std::string line_text;
		if (title_len >= width) {
			for (int i = 0; i < width; ++i)
				line_text += horiz;
		} else {
			int left_pad = (width - title_len) / 2;
			int right_pad = width - title_len - left_pad;
			for (int i = 0; i < left_pad; ++i)
				line_text += horiz;
			line_text += title;
			for (int i = 0; i < right_pad; ++i)
				line_text += horiz;
		}
		lines.push_back({line_text, color});
		return lines;
	}

	std::string display_text = text_;
	if (get_age() > 0) {
		size_t pos = 0;
		int lines = 0;
		while (lines < 3 && pos < display_text.length()) {
			pos = display_text.find('\n', pos);
			if (pos == std::string::npos) {
				pos = display_text.length();
				break;
			}
			pos++;
			lines++;
		}
		if (pos < display_text.length()) {
			display_text = display_text.substr(0, pos);
			if (!display_text.empty() && display_text.back() == '\n') {
				display_text.pop_back(); // Remove trailing newline to append ellipsis cleanly
			}
			display_text += "\n... (reasoning collapsed)";
		}
	}
	return wrap_text("", markdown_utils::align_all_tables(display_text, false), width, color);
}

} // namespace agentlib
