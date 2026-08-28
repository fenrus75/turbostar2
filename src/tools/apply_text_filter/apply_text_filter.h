#pragma once
#include <memory>
#include <string>
#include "agentlib/tool_validator.h"
#include "agentlib/llm_tool.h"

namespace tools
{

struct apply_text_filter_args {
	std::string text;
	std::string path;
	std::string safe_path;
	std::string filter;
	std::string output_path;
	std::string safe_output_path;
};

class apply_text_filter_tool : public agentlib::llm_tool
{
      public:
	explicit apply_text_filter_tool(apply_text_filter_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	apply_text_filter_args args_;
};

class apply_text_filter_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "apply_text_filter";
	}
	std::string get_description() const override
	{
		return "Applies a named content processing or format conversion filter (e.g., converting HTML to Markdown via 'html_to_markdown', aligning tables via 'markdown_align_tables', or sanitizing input via 'strip_ansi'/'strip_utf8') to input text or a workspace/VFS file. Optionally saves converted output directly to a workspace file.";;
	}
	nlohmann::json get_parameters_schema() const override;

	bool is_pure() const override
	{
		return false;
	}

	bool is_pure(const nlohmann::json &args) const override
	{
		if (args.contains("output_path") && args["output_path"].is_string()) {
			std::string out = args["output_path"].get<std::string>();
			if (!out.empty() && !out.starts_with("tmp://") && !out.starts_with("images://")) {
				return false;
			}
		}
		return true;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	mutable apply_text_filter_args parsed_args_;
};

} // namespace tools
