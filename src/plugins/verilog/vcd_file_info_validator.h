#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/verilog/vcd_file_info_tool.h"

namespace tools
{

class vcd_file_info_validator : public agentlib::tool_validator
{
      public:
	vcd_file_info_validator() = default;
	~vcd_file_info_validator() override = default;

	bool is_pure() const override { return true; }
	std::string get_name() const override { return "vcd_file_info"; }
	std::string get_description() const override;
	std::string get_family() const override { return "verilog"; }
	nlohmann::json get_parameters_schema() const override;

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable vcd_file_info_args args_;
};

} // namespace tools
