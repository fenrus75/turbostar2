#include "terminal.h"
#include "../../markdown_utils.h"

namespace agentlib
{

static std::string sanitize_terminal_output(const std::string& input) {
	std::string output;
	output.reserve(input.length());
	size_t i = 0;
	while (i < input.length()) {
		if (input[i] == '\x1b') {
			// Check for ANSI CSI sequence: ESC [ ... [a-zA-Z]
			if (i + 1 < input.length() && input[i + 1] == '[') {
				size_t j = i + 2;
				while (j < input.length()) {
					char c = input[j];
					if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
						// Found the end of the sequence
						i = j + 1;
						break;
					}
					j++;
				}
				if (j == input.length()) {
					// Incomplete sequence at end of string, just replace ESC
					output += ' ';
					i++;
				}
			} else {
				// Unknown escape sequence, replace ESC with space for safety
				output += ' ';
				i++;
			}
		} else {
			output += input[i];
			i++;
		}
	}
	return output;
}

interaction_terminal::interaction_terminal(std::string title, std::string text) : title_(std::move(title)), text_(sanitize_terminal_output(text))
{
}

void interaction_terminal::append_text(const std::string &t)
{
	text_ += sanitize_terminal_output(t);
	invalidate_cache();
}

void interaction_terminal::set_text(const std::string &t)
{
	text_ = sanitize_terminal_output(t);
	invalidate_cache();
}

std::string interaction_terminal::get_raw_text() const
{
	return "[" + title_ + "]\n" + text_;
}

interaction_role interaction_terminal::get_role() const
{
	return interaction_role::terminal;
}

interaction_type interaction_terminal::get_type() const
{
	return interaction_type::terminal;
}


std::vector<interaction_line> interaction_terminal::format_lines(int width, background_mode bg) const
{
	int color = get_color_pair(interaction_role::terminal, bg);
	auto lines = wrap_text("", text_, width, color);
	for (auto &line : lines) {
		int len = markdown_utils::utf8_length(line.text);
		if (len < width) {
			line.text += std::string(width - len, ' ');
		}
	}
	return lines;
}

} // namespace agentlib
