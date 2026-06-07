#pragma once
#include <string>
#include <optional>
#include <memory>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct fs_man_args {
	std::string name;
	std::optional<std::string> section;
};

class fs_man_tool : public agentlib::llm_tool_action {
public:
	explicit fs_man_tool(fs_man_args args);

	bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::string execute(agentlib::tool_context& ctx) override;

private:
	fs_man_args args_;
};

class fs_man_validator : public agentlib::tool_validator {
public:
	std::string get_name() const override { return "fs_man"; }
	std::string get_description() const override {
		return "Lookup and render system man pages (C library calls, command line tools, or system calls) as Markdown. Use to find exact API format and descriptions for system and library functions";
	}

	nlohmann::json get_parameters_schema() const override {
		return {
			{"type", "object"},
			{"properties", {
				{"name", {
					{"type", "string"},
					{"description", "The command or function name to lookup (e.g., 'printf', 'mmap', 'ls')."}
				}},
				{"section", {
					{"type", "string"},
					{"description", "Optional man page section (e.g., '3' for library functions, '1' for commands). If omitted, prioritizes library calls first."}
				}}
			}},
			{"required", nlohmann::json::array({"name"})}
		};
	}

	bool is_pure() const override { return true; }

protected:
	bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

private:
	mutable fs_man_args args_;
};

} // namespace tools