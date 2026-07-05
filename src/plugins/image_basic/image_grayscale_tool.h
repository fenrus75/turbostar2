#pragma once

#include <string>
#include <optional>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct image_grayscale_args {
	std::string name;
	std::string safe_path;
	std::optional<std::string> output;
};

class image_grayscale_tool : public agentlib::llm_tool_action
{
      public:
	explicit image_grayscale_tool(image_grayscale_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	image_grayscale_args args_;
};

} // namespace tools
