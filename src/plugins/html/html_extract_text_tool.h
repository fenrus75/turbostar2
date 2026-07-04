#pragma once

#include <string>
#include <vector>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct html_extract_text_args {
	std::string requested_path;
	std::string safe_path;
	bool rich = true;
};

class html_extract_text_tool : public agentlib::llm_tool_action
{
      public:
	explicit html_extract_text_tool(html_extract_text_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	html_extract_text_args args_;
};

} // namespace tools

namespace html
{
std::string convert_to_markdown(const std::string &html_content, bool rich);
std::string extract_tables(const std::string &html_content);
}
