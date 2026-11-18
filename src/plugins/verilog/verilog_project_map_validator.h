#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/verilog/verilog_project_map_tool.h"

namespace tools {

/**
 * @brief Validator for the read-only verilog_project_map onboarding tool.
 */
class verilog_project_map_validator final : public agentlib::tool_validator {
    public:
	verilog_project_map_validator() = default;
	~verilog_project_map_validator() override = default;

	std::string get_name() const override { return "verilog_project_map"; }
	std::string get_description() const override;
	nlohmann::json get_parameters_schema() const override;
	bool is_pure() const override { return true; }

    protected:
	bool validate_args_impl(const nlohmann::json &untrusted_args,
				const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &untrusted_args) const override;

    private:
	mutable verilog_project_map_args args_;
};

} // namespace tools
