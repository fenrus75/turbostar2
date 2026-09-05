#pragma once
#include <string>
#include <optional>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct get_code_review_item_args {
	int id{0};
};

class get_code_review_item_tool : public agentlib::llm_tool_action {
public:
	explicit get_code_review_item_tool(get_code_review_item_args args);

	bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::string execute(agentlib::tool_context& ctx) override;

private:
	get_code_review_item_args args_;
};

class get_code_review_item_validator : public agentlib::tool_validator {
public:
	std::string get_family() const override { return "code_review"; }
	std::string get_name() const override { return "get_code_review_item"; }
	std::string get_description() const override {
		return "Retrieves the full details of a specific code review item by its unique ID. Access to resolved items is restricted to the verifier role.";
	}

	nlohmann::json get_parameters_schema() const override {
		return {
			{"type", "object"},
			{"properties", {
				{"item_id", {
					{"type", "integer"},
					{"description", "The unique ID of the code review item to retrieve."}
				}},
				{"id", {
					{"type", "integer"},
					{"description", "Alias for item_id."}
				}}
			}}
		};
	}

	bool is_pure() const override { return true; }

protected:
	bool validate_args_impl(const nlohmann::json& raw_args, const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override;

private:
	mutable get_code_review_item_args args_;
};

} // namespace tools
