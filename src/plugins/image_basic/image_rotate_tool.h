#pragma once

#include <string>
#include <optional>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct image_rotate_args {
	std::string name;
	std::string safe_path;
	double degrees = 0.0;
	std::optional<std::string> output;
};

class image_rotate_tool : public agentlib::llm_tool_action
{
      public:
	explicit image_rotate_tool(image_rotate_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	image_rotate_args args_;
};

} // namespace tools
