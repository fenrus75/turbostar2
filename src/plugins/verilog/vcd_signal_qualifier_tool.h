#pragma once

#include <string>
#include <map>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct vcd_signal_qualifier_args {
	std::string requested_path;
	std::string safe_path;
	std::string signal;
	std::string qualifier;
	std::map<std::string, std::string> bindings;
	bool clear{false};
	bool global{true};
};

class vcd_signal_qualifier_tool : public agentlib::llm_tool_action
{
      public:
	explicit vcd_signal_qualifier_tool(vcd_signal_qualifier_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	vcd_signal_qualifier_args args_;
};

} // namespace tools
