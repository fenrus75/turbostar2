#include "tool_interaction.h"
#include "../../markdown_utils.h"

namespace agentlib
{

std::vector<interaction_line> interaction_tool_call::format_lines(int width, background_mode bg) const
{
	int color = get_color_pair(interaction_role::thinking, bg);
	return wrap_text("* Executing tool: ", markdown_utils::align_all_tables(text_, true), width, color);
}

std::vector<interaction_line> interaction_tool_result::format_lines(int width, background_mode bg) const
{
	int color = get_color_pair(interaction_role::agent, bg);
	return wrap_text("  ↳ Result: ", markdown_utils::align_all_tables(text_, true), width, color);
}

} // namespace agentlib

#include <sstream>

namespace agentlib
{

std::vector<interaction_line> interaction_fs_grep_files::format_lines(int width, background_mode bg) const
{
	std::vector<interaction_line> lines;
	int default_color = get_color_pair(interaction_role::agent, bg);
	int header_color = get_color_pair(interaction_role::header, bg);
	int note_color = get_color_pair(interaction_role::system, bg); // Red

	// Magenta is base + 6
	int base_color = 50;
	if (bg == background_mode::cyan) base_color = 60;
	if (bg == background_mode::white) base_color = 70;
	int magenta_color = base_color + 6;

	std::stringstream ss(result_);
	std::string line;
	bool in_code_block = false;

	while (std::getline(ss, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();

		if (line.starts_with("```")) {
			in_code_block = !in_code_block;
			// Optionally skip printing the ``` line itself
			continue; 
		}

		int current_color = default_color;
		if (in_code_block) {
			current_color = magenta_color;
		} else if (line.starts_with("### ")) {
			current_color = header_color;
		} else if (line.starts_with("*Note:") || line.starts_with("Error:")) {
			current_color = note_color;
		}

		// For simple formatting, just add the line directly if it fits, else truncate/wrap
		auto wrapped = wrap_text("", line, width, current_color);
		lines.insert(lines.end(), wrapped.begin(), wrapped.end());
	}

	return lines;
}

} // namespace agentlib
