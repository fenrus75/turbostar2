#include "agent_debug_coredump.h"
#include "crashdump_manager.h"
#include <nlohmann/json.hpp>
#include <format>

namespace tools
{

bool agent_debug_coredump_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.doc_provider) {
		out_error = "Execution Error: No document provider context available.";
		return false;
	}
	return true;
}

std::string agent_debug_coredump_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.doc_provider) {
		set_failure(ctx, "Internal Error: document provider is not available");
		return "Error: Internal engine type mismatch.";
	}

	agentlib::start_app_result res = ctx.doc_provider->start_coredump_gdb(args_.crash_id);
	if (res.gdb_run_id < 0) {
		set_failure(ctx, "Failed to debug coredump.");
		return "Error: Failed to launch coredump GDB session.";
	}

	crash_frame_info frame_info = crashdump_manager::get_instance().get_crash_frame_info(args_.crash_id);

	std::string gdb_cmd_example;
	if (!frame_info.suggested_var.empty()) {
		gdb_cmd_example = std::format("frame {}\\nprint {}\\n", frame_info.frame_number, frame_info.suggested_var);
	} else {
		gdb_cmd_example = std::format("frame {}\\ninfo locals\\n", frame_info.frame_number);
	}

	std::string site_desc;
	if (!frame_info.function_name.empty()) {
		site_desc = std::format(" (Frame {} in {} at {})", frame_info.frame_number, frame_info.function_name, frame_info.location);
	}

	nlohmann::json output = {
	    {"gdb_run_id", res.gdb_run_id},
	    {"crash_frame", frame_info.frame_number},
	    {"function", frame_info.function_name},
	    {"location", frame_info.location},
	    {"suggested_var", frame_info.suggested_var},
	    {"instructions", std::format("Coredump GDB session started (gdb_run_id: {}). Crash site identified{}.\n\nNext Steps:\n1. Send GDB commands to inspect crash frame:\n   agent_write_to_run(run_id={}, data=\"{}\", output=true)\n2. Clean up when finished:\n   agent_terminate_run(run_id={})\n\nFull workflow reference: system://tools_detailed.md?search=agent_debug_coredump", res.gdb_run_id, site_desc, res.gdb_run_id, gdb_cmd_example, res.gdb_run_id)}};

	set_success(ctx, std::format("Started coredump debugger for crash_id {}", args_.crash_id));
	return output.dump();
}


} // namespace tools
