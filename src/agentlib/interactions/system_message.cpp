#include "system_message.h"
#include "../../markdown_utils.h"

namespace agentlib
{

std::vector<interaction_line> interaction_system_message::format_lines(int width) const
{
	return wrap_text("", markdown_utils::align_all_tables(text_, true), width, 2);
}

} // namespace agentlib
