#pragma once

#include <string>
#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_validator.h"

namespace tools
{

struct image_export_args {
	std::string name;
	std::string safe_path;
	std::string original_filename;
};

class image_export_tool : public agentlib::llm_tool_action
{
      public:
	explicit image_export_tool(image_export_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	image_export_args args_;
};

class image_export_validator : public agentlib::tool_validator
{
      public:
	image_export_validator() = default;
	~image_export_validator() override = default;

	bool is_pure() const override { return false; }
	std::string get_name() const override { return "image_export"; }
	std::string get_description() const override
	{
		return "Exports an image from the virtual image VFS database (image://) as a real file in the project workspace.";
	}
	std::string get_family() const override { return "image"; }
	nlohmann::json get_parameters_schema() const override;

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable image_export_args parsed_args_;
};

} // namespace tools
