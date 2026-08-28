#pragma once
#include <string>
#include "agentlib/tool_validator.h"
#include "markdown_extract_tool.h"

namespace tools {

class markdown_extract_validator : public agentlib::tool_validator {
public:
	std::string get_name() const override { return "markdown_extract"; }
	std::string get_family() const override { return "base"; }
	std::string get_description() const override {
		return "Extract specific sections, directives, or topics from a Markdown document or VFS file (e.g. 'docs/design.md' or 'system://tools_detailed.md'). Use this to extract targeted documentation sections without reading massive files into context.";;
	}

	nlohmann::json get_parameters_schema() const override {
		return {
			{"type", "object"},
			{"properties", {
				{"path", {
					{"type", "string"},
					{"description", "Relative path under the project workspace or VFS URI (e.g., 'docs/design.md' or 'system://tools_detailed.md')."}
				}},
				{"query", {
					{"type", "string"},
					{"description", "The specific topic, directive name (e.g., 'ProtectKernelTunables'), question, or section heading to extract."}
				}},
				{"output_path", {
					{"type", "string"},
					{"description", "Optional. Relative file path under the project workspace or VFS URI (e.g. 'tmp://extract.md') to save the extracted Markdown result."}
				}},
				{"async", {
					{"type", "boolean"},
					{"description", "Optional. If true, runs extraction in the background. Default is false (synchronous extraction)."}
				}}
			}},
			{"required", nlohmann::json::array({"path", "query"})}
		};
	}

	std::vector<agentlib::tool_example> get_examples() const override;


protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

private:
	mutable markdown_extract_args args_;
};

} // namespace tools
