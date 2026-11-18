#pragma once

#include "agentlib/llm_tool_action.h"

#include <string>

namespace tools {

struct verilog_project_map_args {
	std::string module;
	int max_depth{16};
};

/**
 * @brief Read-only onboarding tool that maps a Verilog/SystemVerilog project's module hierarchy and files.
 */
class verilog_project_map_tool : public agentlib::llm_tool_action {
    public:
	explicit verilog_project_map_tool(verilog_project_map_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

    private:
	verilog_project_map_args args_;
};

} // namespace tools
