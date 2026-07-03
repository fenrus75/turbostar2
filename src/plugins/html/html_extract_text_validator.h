#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/html/html_extract_text_tool.h"

namespace tools
{

class html_extract_text_validator : public agentlib::tool_validator
{
      public:
	html_extract_text_validator() = default;
	~html_extract_text_validator() override = default;

	bool is_pure() const override { return true; }
	std::string get_name() const override { return "html_extract_text"; }
	std::string get_description() const override
	{
		return "Extracts structured text from an HTML document as Markdown, keeping lists, headers, code blocks, tables, and links.";
	}
	std::string get_family() const override { return "html"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"path", {{"type", "string"}, {"description", "The path to the HTML file relative to the project root."}}},
		      {"rich", {{"type", "boolean"}, {"description", "If true (default), inline elements (bold/italic) will be preserved in Markdown format. If false, they will be stripped."}}}}},
		    {"required", nlohmann::json::array({"path"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable html_extract_text_args args_;
};

} // namespace tools
