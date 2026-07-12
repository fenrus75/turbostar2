#pragma once

#include <string>
#include <optional>
#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_validator.h"

#include "agentlib/interactions/image_tool.h"

namespace tools
{

struct image_import_args {
	std::optional<std::string> filename;
	std::optional<std::string> URL;
	std::string output;
};

class image_import_tool : public agentlib::llm_tool_action
{
      public:
	explicit image_import_tool(image_import_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;
	std::shared_ptr<agentlib::agent_interaction> get_interaction() const override { return interaction_; }

      private:
	image_import_args args_;
	std::shared_ptr<agentlib::interaction_image_tool> interaction_;
};

class image_import_validator : public agentlib::tool_validator
{
      public:
	image_import_validator() = default;
	~image_import_validator() override = default;

	bool is_pure() const override { return false; }
	std::string get_name() const override { return "image_import"; }
	std::string get_description() const override
	{
		return "Imports an image from a local file or a web URL into the virtual VFS image database (image://).";
	}
	std::string get_family() const override { return "image"; }
	nlohmann::json get_parameters_schema() const override;

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable image_import_args parsed_args_;
};

} // namespace tools
