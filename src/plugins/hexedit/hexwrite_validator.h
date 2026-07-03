#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/hexedit/hexwrite_tool.h"

namespace tools
{

class hexwrite_validator : public agentlib::tool_validator
{
      public:
	hexwrite_validator() = default;
	~hexwrite_validator() override = default;

	bool is_pure() const override { return false; }
	std::string get_name() const override { return "hexwrite"; }
	std::string get_description() const override;
	std::string get_family() const override { return "hexedit"; }
	nlohmann::json get_parameters_schema() const override;

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable hexwrite_args args_;
};

} // namespace tools
