#pragma once
#include <string>
#include <optional>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct list_code_review_items_args {
	std::string filename;
	std::string severity;
	std::string state;
	bool include_resolved{false};
};

class list_code_review_items_tool : public agentlib::llm_tool_action {
public:
	explicit list_code_review_items_tool(list_code_review_items_args args);

	bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::string execute(agentlib::tool_context& ctx) override;

private:
	list_code_review_items_args args_;
};

class list_code_review_items_validator : public agentlib::tool_validator {
public:
	std::string get_family() const override { return "code_review"; }
	std::string get_name() const override { return "list_code_review_items"; }
	std::string get_description() const override {
		return "Lists all code review items as a compact Markdown table, with optional filters for filename, severity, state, and resolution status.";
	}

	nlohmann::json get_parameters_schema() const override {
		return {
			{"type", "object"},
			{"properties", {
				{"path", {
					{"type", "string"},
					{"description", "Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt'). Optional path prefix to filter items (e.g. 'src/')."}
				}},
				{"severity", {
					{"type", "string"},
					{"description", "Optional severity filter. Must be one of: nit, low, medium, high, critical. Specifying a level returns all items of that severity or more severe (e.g. 'medium' returns medium, high, and critical issues)."}
				}},
				{"state", {
					{"type", "string"},
					{"description", "Optional state filter. Must be one of: active (default), new, confirmed, disputed, stale, resolved, verified-fixed, all."}
				}},
				{"include_resolved", {
					{"type", "boolean"},
					{"description", "If true, lists resolved/verified items. (Only allowed/visible for verifier role)."}
				}}
			}}
		};
	}

	bool is_pure() const override { return true; }

protected:
	bool validate_args_impl(const nlohmann::json& raw_args, const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override;

private:
	mutable list_code_review_items_args args_;
};

} // namespace tools
