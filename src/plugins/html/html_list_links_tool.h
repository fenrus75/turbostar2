#pragma once

#include <string>
#include <vector>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct html_list_links_args {
	std::string requested_path;
	std::string safe_path;
};

class html_list_links_tool : public agentlib::llm_tool_action
{
      public:
	explicit html_list_links_tool(html_list_links_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	html_list_links_args args_;
};

} // namespace tools
