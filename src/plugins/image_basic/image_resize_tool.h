#pragma once

#include <string>
#include <optional>
#include "agentlib/llm_tool_action.h"

#include "agentlib/interactions/image_tool.h"

namespace tools
{

struct image_resize_args {
	std::string original_uri;
	std::string safe_path;
	std::optional<int> newX;
	std::optional<int> newY;
	std::optional<double> ratio;
	std::optional<std::string> output;
};

class image_resize_tool : public agentlib::llm_tool_action
{
      public:
	explicit image_resize_tool(image_resize_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;
	std::shared_ptr<agentlib::agent_interaction> get_interaction() const override { return interaction_; }

      private:
	image_resize_args args_;
	std::shared_ptr<agentlib::interaction_image_tool> interaction_;
};

} // namespace tools
