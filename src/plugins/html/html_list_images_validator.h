#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/html/html_list_images_tool.h"

namespace tools
{

class html_list_images_validator : public agentlib::tool_validator
{
      public:
	html_list_images_validator() = default;
	~html_list_images_validator() override = default;

	bool is_pure() const override { return true; }
	std::string get_name() const override { return "html_list_images"; }
	std::string get_description() const override
	{
		return "Extracts all image alt texts and source URLs from an HTML document.";
	}
	std::string get_family() const override { return "html"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"path", {{"type", "string"}, {"description", "The path to the HTML file relative to the project root."}}}}},
		    {"required", nlohmann::json::array({"path"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable html_list_images_args args_;
};

} // namespace tools
