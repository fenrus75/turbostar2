#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/html/html_extract_tables_tool.h"

namespace tools
{

class html_extract_tables_validator : public agentlib::tool_validator
{
      public:
	html_extract_tables_validator() = default;
	~html_extract_tables_validator() override = default;

	bool is_pure() const override { return true; }
	std::string get_name() const override { return "html_extract_tables"; }
	std::string get_description() const override
	{
		return "Parses an HTML file and extracts all HTML tables, converting them to beautifully aligned markdown tables. "
		       "Includes active heading headers (h1, h2, h3) preceding each table.";
	}
	std::string get_family() const override { return "html"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"path", {{"type", "string"}, {"description", "The path to the HTML file relative to the project root."}}},
		      {"output_path", {{"type", "string"}, {"description", "Optional path relative to the project root to write the output markdown file to."}}}}},
		    {"required", nlohmann::json::array({"path"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable html_extract_tables_args args_;
};

} // namespace tools
