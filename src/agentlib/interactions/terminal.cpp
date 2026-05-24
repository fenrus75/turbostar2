#include "terminal.h"

namespace agentlib
{

interaction_terminal::interaction_terminal(std::string title, std::string text) : title_(std::move(title)), text_(std::move(text))
{
	set_boxed(true, 36, title_); // 36 is Bright White on Black (Terminal Border)
}

void interaction_terminal::append_text(const std::string &t)
{
	text_ += t;
	invalidate_cache();
}

void interaction_terminal::set_text(const std::string &t)
{
	text_ = t;
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
	return wrap_text("", text_, width, color);
}

} // namespace agentlib
