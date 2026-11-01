#pragma once

#include <string>
#include <map>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct vcd_signal_labels_args {
	std::string requested_path;
	std::string safe_path;
	std::string signal;
	std::map<std::string, std::string> labels;
	bool clear{false};
};

class vcd_signal_labels_tool : public agentlib::llm_tool_action
{
      public:
	explicit vcd_signal_labels_tool(vcd_signal_labels_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	vcd_signal_labels_args args_;
};

} // namespace tools
