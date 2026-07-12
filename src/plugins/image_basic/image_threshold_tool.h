#pragma once

#include <string>
#include <optional>
#include "agentlib/llm_tool_action.h"

#include "agentlib/interactions/image_tool.h"

namespace tools
{

struct image_threshold_args {
	std::string name;
	std::string safe_path;
	std::optional<double> level;
	int windowWidth = 16;
	int windowHeight = 16;
	double offset = 0.0;
	std::optional<std::string> output;
};

class image_threshold_tool : public agentlib::llm_tool_action
{
      public:
	explicit image_threshold_tool(image_threshold_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;
	std::shared_ptr<agentlib::agent_interaction> get_interaction() const override { return interaction_; }

      private:
	image_threshold_args args_;
	std::shared_ptr<agentlib::interaction_image_tool> interaction_;
};

} // namespace tools
