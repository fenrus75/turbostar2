#include "plugins/verilog/verilog_project_map_validator.h"
#include "agentlib/tool_registry.h"

namespace tools {

std::string verilog_project_map_validator::get_description() const
{
	return "Maps a Verilog/SystemVerilog project's module instantiation hierarchy, RTL/testbench subdirectories, "
	       "and testbench coverage in one fast read-only pass. Ideal as the first step when exploring a project.";
}

nlohmann::json verilog_project_map_validator::get_parameters_schema() const
{
	return {
		{"type", "object"},
		{"properties", {
			{"module", {
				{"type", "string"},
				{"description", "Optional root module name to map. Defaults to top_module from turbostar.json or auto-detected root modules."}
			}},
			{"max_depth", {
				{"type", "integer"},
				{"description", "Maximum hierarchy depth to display (default: 16)."}
			}}
		}}
	};
}

bool verilog_project_map_validator::validate_args_impl(const nlohmann::json &untrusted_args,
							 const agentlib::tool_context &/*ctx*/,
							 std::string &out_error) const
{
	args_ = verilog_project_map_args{};
	if (!untrusted_args.is_object()) {
		out_error = "Arguments must be a JSON object";
		return false;
	}
	if (untrusted_args.contains("module") && !untrusted_args["module"].is_null()) {
		if (!untrusted_args["module"].is_string()) {
			out_error = "'module' must be a string";
			return false;
		}
		args_.module = untrusted_args["module"].get<std::string>();
	}
	if (untrusted_args.contains("max_depth") && !untrusted_args["max_depth"].is_null()) {
		if (!untrusted_args["max_depth"].is_number_integer()) {
			out_error = "'max_depth' must be an integer";
			return false;
		}
		args_.max_depth = std::clamp(untrusted_args["max_depth"].get<int>(), 1, 64);
	}
	return true;
}

std::unique_ptr<agentlib::llm_tool> verilog_project_map_validator::create_tool_impl(
	const nlohmann::json &/*untrusted_args*/) const
{
	return std::make_unique<verilog_project_map_tool>(args_);
}

} // namespace tools

extern "C" {

void register_verilog_project_map(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::verilog_project_map_validator>(); });
}

void unregister_verilog_project_map(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("verilog_project_map");
}

}
