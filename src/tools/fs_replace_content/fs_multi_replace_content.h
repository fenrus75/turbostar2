#pragma once

#include "agentlib/tool_context.h"
#include "agentlib/tool_registry.h"
#include "fs_replace_engine.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace tools {

struct fs_multi_replace_content_args {
	std::string path;
	std::string safe_path;
	std::vector<replace_chunk> chunks;
	bool strict{false};
};

class fs_multi_replace_content_tool : public agentlib::llm_tool {
public:
	explicit fs_multi_replace_content_tool(fs_multi_replace_content_args args);

	std::string execute(agentlib::tool_context &ctx) override;
	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;

private:
	fs_multi_replace_content_args args_;
	std::shared_ptr<agentlib::agent_interaction> interaction_;
};

class fs_multi_replace_content_validator : public agentlib::tool_validator {
public:
	std::string get_name() const override { return "fs_multi_replace_content"; }
	std::string get_description() const override {
		return "Replaces multiple non-contiguous blocks of text across a single file in a single atomic transaction. "
		       "If any chunk fails to match or breaks syntax/brace-balance in strict mode, all changes are cleanly rolled back.";
	}

	nlohmann::json get_parameters_schema() const override {
		return {
			{"type", "object"},
			{"properties", {
				{"path", {
					{"type", "string"},
					{"description", "Path to the file to edit, relative to the project root."}
				}},
				{"chunks", {
					{"type", "array"},
					{"description", "List of replacement chunks to apply to the target file."},
					{"items", {
						{"type", "object"},
						{"properties", {
							{"target_content", {
								{"type", "string"},
								{"description", "The exact block of text in the file to be replaced."}
							}},
							{"replacement_content", {
								{"type", "string"},
								{"description", "The new text block that will replace target_content."}
							}},
							{"line_hint", {
								{"type", "integer"},
								{"description", "Optional 1-based line number hint for where target_content is located."}
							}},
							{"function_scope", {
								{"type", "string"},
								{"description", "Optional enclosing function, method, or class name (e.g. 'validate_args_impl') to scope search."}
							}},
							{"start_line", {
								{"type", "integer"},
								{"description", "Optional 1-based start line boundary for target_content search."}
							}},
							{"end_line", {
								{"type", "integer"},
								{"description", "Optional 1-based end line boundary for target_content search."}
							}}
						}},
						{"required", nlohmann::json::array({"target_content", "replacement_content"})}
					}}
				}},
				{"strict", {
					{"type", "boolean"},
					{"description", "Optional. If true, reject (and revert) the edit if it leaves braces unbalanced."}
				}}
			}},
			{"required", nlohmann::json::array({"path", "chunks"})}
		};
	}

protected:
	bool validate_args_impl(const nlohmann::json &raw_args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

private:
	mutable fs_multi_replace_content_args args_;
};

} // namespace tools
