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
