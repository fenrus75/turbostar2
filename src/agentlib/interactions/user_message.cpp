#include "user_message.h"

namespace agentlib
{

std::vector<interaction_line> interaction_user_message::format_lines(int width, background_mode bg) const
{
	int color = get_color_pair(interaction_role::user, bg);
	return wrap_text("> ", text_, width, color);
}

} // namespace agentlib
