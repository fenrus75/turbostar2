#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/verilog/verilog_update_timing_tool.h"

namespace tools
{

class verilog_update_timing_validator : public agentlib::tool_validator
{
      public:
	verilog_update_timing_validator() = default;
	~verilog_update_timing_validator() override = default;

	bool is_pure() const override { return false; }
	std::string get_name() const override { return "verilog_update_timing"; }
	std::string get_description() const override;
	std::string get_family() const override { return "verilog"; }
	nlohmann::json get_parameters_schema() const override;

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
	                        std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable verilog_update_timing_args args_;
};

} // namespace tools
