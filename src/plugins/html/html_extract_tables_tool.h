#pragma once

#include <string>
#include <vector>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct html_extract_tables_args {
	std::string requested_path;
	std::string safe_path;
	std::string output_path;
	std::string safe_output_path;
};

class html_extract_tables_tool : public agentlib::llm_tool_action
{
      public:
	explicit html_extract_tables_tool(html_extract_tables_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	html_extract_tables_args args_;
};

} // namespace tools
