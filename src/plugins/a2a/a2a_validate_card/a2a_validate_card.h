#pragma once
#include <optional>
#include <string>
#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_validator.h"

namespace tools
{

struct a2a_validate_card_args {
	std::string requested_path;
	std::string safe_path;
	std::string card_data;
};

class a2a_validate_card_tool : public agentlib::llm_tool_action
{
      public:
	explicit a2a_validate_card_tool(a2a_validate_card_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	a2a_validate_card_args args_;
};

class a2a_validate_card_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "a2a_validate_card";
	}
	std::string get_description() const override
	{
		return "Validates an A2A Agent Card JSON file or string against the formal A2A Agent Card specification.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"path",
		       {{"type", "string"},
			{"description",
			 "Relative path under the project workspace or VFS URI (e.g., 'tmp://agent.card.json'). Optional path to the A2A card JSON file to validate."}}},
		      {"card_data",
		       {{"type", "string"},
			{"description", "Optional raw JSON string of the A2A card to validate directly."}}}}},
		    {"required", nlohmann::json::array()}};
	}

	std::string get_family() const override
	{
		return "a2a";
	}

	bool is_pure() const override
	{
		return true;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	mutable a2a_validate_card_args args_;
};

} // namespace tools
