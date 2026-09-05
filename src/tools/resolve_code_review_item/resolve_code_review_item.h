#pragma once
#include <string>
#include <optional>
#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_validator.h"

namespace tools {

struct resolve_code_review_item_args {
	std::vector<int> item_ids;
	std::string commit_hash;
};

class resolve_code_review_item_tool : public agentlib::llm_tool_action {
public:
	explicit resolve_code_review_item_tool(resolve_code_review_item_args args);

	bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::string execute(agentlib::tool_context& ctx) override;

private:
	resolve_code_review_item_args args_;
};

class resolve_code_review_item_validator : public agentlib::tool_validator {
public:
	// Pure Domain 3 (Editor Internal Metadata): Resolves internal code review items in editor state.
	bool is_pure() const override { return true; }
	std::string get_family() const override { return "code_review"; }
	std::string get_name() const override { return "resolve_code_review_item"; }
	std::string get_description() const override {
		return "Resolves a code review item (or multiple items in batch) by marking their state as 'resolved' and recording the commit hash where the fix was implemented. Only accessible by developer and verifier roles.";
	}

	nlohmann::json get_parameters_schema() const override {
		return {
			{"type", "object"},
			{"properties", {
				{"item_id", {
					{"type", "integer"},
					{"description", "The unique ID of the code review item to resolve."}
				}},
				{"item_ids", {
					{"type", "array"},
					{"items", {{"type", "integer"}}},
					{"description", "Optional array of code review item IDs to resolve in batch."}
				}},
				{"id", {
					{"type", "integer"},
					{"description", "Alias for item_id."}
				}},
				{"ids", {
					{"type", "array"},
					{"items", {{"type", "integer"}}},
					{"description", "Alias for item_ids."}
				}},
				{"commit_hash", {
					{"type", "string"},
					{"description", "The git commit hash containing the resolution/fix."}
				}}
			}},
			{"required", nlohmann::json::array({"commit_hash"})}
		};
	}

	bool is_allowed_for_agent(const agentlib::agent_properties &properties) const override {
		return properties.role == agentlib::agent_role::developer || properties.role == agentlib::agent_role::verifier;
	}

protected:
	bool validate_args_impl(const nlohmann::json& raw_args, const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override;

private:
	mutable resolve_code_review_item_args args_;
};

} // namespace tools
