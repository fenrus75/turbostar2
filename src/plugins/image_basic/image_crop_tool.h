#pragma once

#include <string>
#include <optional>
#include "agentlib/llm_tool_action.h"

#include "agentlib/interactions/image_tool.h"

namespace tools
{

struct image_crop_args {
	std::string name;
	std::string safe_path;
	int width = 0;
	int height = 0;
	int x = 0;
	int y = 0;
	std::optional<std::string> output;
};

class image_crop_tool : public agentlib::llm_tool_action
{
      public:
	explicit image_crop_tool(image_crop_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;
	std::shared_ptr<agentlib::agent_interaction> get_interaction() const override { return interaction_; }

      private:
	image_crop_args args_;
	std::shared_ptr<agentlib::interaction_image_tool> interaction_;
};

} // namespace tools
