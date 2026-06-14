#pragma once
#include <string>
#include <optional>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct update_code_review_item_args {
	int id{0};
	std::optional<std::string> state;
	std::optional<std::string> severity;
	std::optional<std::string> description;
	std::optional<std::string> proposed_fix;
};

class update_code_review_item_tool : public agentlib::llm_tool_action {
public:
	explicit update_code_review_item_tool(update_code_review_item_args args);

	bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::string execute(agentlib::tool_context& ctx) override;

private:
	update_code_review_item_args args_;
};

class update_code_review_item_validator : public agentlib::tool_validator {
public:
	std::string get_name() const override { return "update_code_review_item"; }
	std::string get_description() const override {
		return "Updates one or more fields of an existing code review item in the database.";
	}

	nlohmann::json get_parameters_schema() const override {
		return {
			{"type", "object"},
			{"properties", {
				{"id", {
					{"type", "integer"},
					{"description", "The unique ID of the code review item to update."}
				}},
				{"state", {
					{"type", "string"},
					{"description", "The new state. Must be one of: invalid, new, confirmed, disputed, stale, resolved, verified-fixed."}
				}},
				{"severity", {
					{"type", "string"},
					{"description", "The new severity rating. Must be one of: nit, low, medium, high, critical."}
				}},
				{"description", {
					{"type", "string"},
					{"description", "Updated explanation of the code issue."}
				}},
				{"proposed_fix", {
					{"type", "string"},
					{"description", "Updated proposed code snippet or description."}
				}}
			}},
			{"required", nlohmann::json::array({"id"})}
		};
	}

protected:
	bool validate_args_impl(const nlohmann::json& raw_args, const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override;

private:
	mutable update_code_review_item_args args_;
};

} // namespace tools
