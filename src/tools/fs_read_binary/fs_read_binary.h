#pragma once
#include <optional>
#include <string>
#include "../../agentlib/llm_tool.h"

namespace tools
{

struct fs_read_binary_args {
	std::string requested_path;
	int start_offset;
	int size;
	std::string safe_path;
	std::string format;
};

class fs_read_binary_tool : public agentlib::llm_tool
{
      public:
	explicit fs_read_binary_tool(fs_read_binary_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	fs_read_binary_args args_;
};

} // namespace tools