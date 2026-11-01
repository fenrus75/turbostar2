#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/verilog/vcd_signal_labels_tool.h"

namespace tools
{

class vcd_signal_labels_validator : public agentlib::tool_validator
{
      public:
	vcd_signal_labels_validator() = default;
	~vcd_signal_labels_validator() override = default;

	// Pure in memory, though may write sidecar file if permissions allow
	bool is_pure() const override { return true; }
	std::string get_name() const override { return "vcd_signal_labels"; }
	std::string get_description() const override;
	std::string get_family() const override { return "verilog"; }
	nlohmann::json get_parameters_schema() const override;

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable vcd_signal_labels_args args_;
};

} // namespace tools
