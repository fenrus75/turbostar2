#include "llm_response.h"
#include "../../markdown_utils.h"

namespace agentlib
{

std::vector<interaction_line> interaction_llm_response::format_lines(int width) const
{
	return wrap_text("", markdown_utils::align_all_tables(text_, true), width, 1);
}

} // namespace agentlib
