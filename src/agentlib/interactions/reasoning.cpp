#include "reasoning.h"
#include "../../markdown_utils.h"

namespace agentlib
{

std::vector<interaction_line> interaction_reasoning::format_lines(int width, background_mode bg) const
{
	int color = get_color_pair(interaction_role::thinking, bg);

	std::string display_text = text_;
	
	if (get_age() > 5) {
		display_text = "... (reasoning collapsed)";
	} else if (get_age() > 0) {
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
