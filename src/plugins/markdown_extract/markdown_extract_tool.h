#pragma once
#include <string>
#include "agentlib/llm_tool_action.h"

namespace tools {

struct markdown_extract_args {
	std::string path;
	std::string query;
	std::string output_path;
	bool is_async{false};
};

class markdown_extract_tool : public agentlib::llm_tool_action {
public:
	explicit markdown_extract_tool(markdown_extract_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

private:
	markdown_extract_args args_;
};

} // namespace tools
