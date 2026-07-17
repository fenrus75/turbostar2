#pragma once
#include <string>
#include <optional>
#include <memory>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct fs_man_search_args {
	std::string query;
	std::optional<std::string> section;
};

class fs_man_search_tool : public agentlib::llm_tool_action {
public:
	explicit fs_man_search_tool(fs_man_search_args args);

	bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::string execute(agentlib::tool_context& ctx) override;

private:
	fs_man_search_args args_;
};

class fs_man_search_validator : public agentlib::tool_validator {
public:
	std::string get_name() const override { return "fs_man_search"; }
	std::string get_description() const override {
		return "Search system manual page names and descriptions for a keyword (similar to 'man -k' or 'apropos'). Returns matching commands, system calls, or library functions with their section numbers and descriptions in a markdown table.";
	}

	nlohmann::json get_parameters_schema() const override {
		return {
			{"type", "object"},
			{"properties", {
				{"query", {
					{"type", "string"},
					{"description", "The keyword or search phrase (e.g., 'socket', 'pthread', 'printf')."}
				}},
				{"section", {
					{"type", "string"},
					{"description", "Optional section to filter results (e.g., '1' for commands, '2' for system calls, '3' for library functions)."}
				}}
			}},
			{"required", nlohmann::json::array({"query"})}
		};
	}

	bool is_pure() const override { return true; }

protected:
	bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

private:
	mutable fs_man_search_args args_;
};

} // namespace tools
