#pragma once

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct verilog_formal_args {
	std::string requested_module;               // Optional target RTL module; if empty, runs all modules with formal/*_props.sv
	std::string mode{"all"};                    // "bmc", "prove", "cover", "prove+cover", "bmc+cover", or "all" (prove + cover)
	int depth{20};                              // BMC / prove / cover depth in steps
	std::string solver{"auto"};                 // "auto", "bitwuzla", "yices", or "z3"
	int timeout_sec{60};                        // Per-module SBY timeout in seconds
	std::map<std::string, std::string> parameters; // Optional Verilog parameter overrides (e.g. COUNTER_BITS=4)
	bool verbose{false};                        // When true, inline all passing coverpoint tables (full report is always in system://verilog/formal.md)
};

class verilog_formal_tool : public agentlib::llm_tool_action
{
      public:
	explicit verilog_formal_tool(verilog_formal_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

	// Static audit: returns declared output/inout ports of module_name that are not referenced
	// (directly or via local helper assignments / bind remappings) by any `assert` in the
	// module's RTL source or bound formal property files.
	static std::vector<std::string> find_unchecked_output_ports(std::string_view module_name,
	                                                            std::span<const std::string> rtl_files,
	                                                            std::span<const std::string> formal_files);

	// Static audit: returns true if the bound formal property files contain at least one `assume`
	// constraint but zero `cover` reachability statements (risk of undetected vacuous proofs).
	static bool has_assumes_without_covers(std::span<const std::string> formal_files);

      private:
	verilog_formal_args args_;
};

} // namespace tools
