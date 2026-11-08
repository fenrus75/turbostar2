#include "plugins/verilog/verilog_logic_depth_validator.h"
#include "agentlib/tool_registry.h"

namespace tools
{

std::string verilog_logic_depth_validator::get_description() const
{
	return "Analyzes the structural layers of logic (LUT/gate depth) of a Verilog/SystemVerilog module to evaluate timing feasibility pre-place-and-route. Reports In->Reg, Reg->Out, Reg->Reg, and In->Out depths along with the top worst critical paths mapped to source RTL coordinates.";
}

nlohmann::json verilog_logic_depth_validator::get_parameters_schema() const
{
	return nlohmann::json{
		{"type", "object"},
		{"properties", {
			{"module", {
				{"type", "string"},
				{"description", "Optional. The target module name to analyze. If omitted, auto-detected from 'file' or the netlist top module."}
			}},
			{"file", {
				{"type", "string"},
				{"description", "Optional. Path to the SystemVerilog/Verilog source file defining the module."}
			}},
			{"netlist", {
				{"type", "string"},
				{"description", "Optional. Path to a pre-generated Yosys JSON netlist. If omitted, runs Yosys synthesis on-the-fly."}
			}},
			{"target", {
				{"type", "string"},
				{"enum", {"Generic LUT4", "Generic LUT6", "Generic Gates", "iCE40"}},
				{"description", "Optional. Synthesis/timing target mode. Defaults to 'Generic LUT4' (standard 4-LUT FPGA)."}
			}},
			{"top_k", {
				{"type", "integer"},
				{"description", "Optional. Maximum number of worst critical paths to report (default 3)."}
			}},
			{"write_sidecar", {
				{"type", "boolean"},
				{"description", "Optional. If true, writes or updates the <module>.timing.md sidecar file next to the source file (default false)."}
			}}
		}}
	};
}

bool verilog_logic_depth_validator::validate_args_impl(const nlohmann::json &raw_json,
                                                       const agentlib::tool_context &ctx,
                                                       std::string &out_error) const
{
	args_ = verilog_logic_depth_args{};

	if (raw_json.contains("module") && raw_json["module"].is_string()) {
		args_.requested_module = raw_json["module"].get<std::string>();
	}

	if (raw_json.contains("file") && raw_json["file"].is_string()) {
		args_.requested_file = raw_json["file"].get<std::string>();
		std::string sec_err;
		if (!ctx.fs_security.validate_access(args_.requested_file, agentlib::access_type::read,
		                                    args_.safe_file, sec_err)) {
			out_error = sec_err;
			return false;
		}
	}

	if (raw_json.contains("netlist") && raw_json["netlist"].is_string()) {
		args_.requested_netlist = raw_json["netlist"].get<std::string>();
		std::string sec_err;
		if (!ctx.fs_security.validate_access(args_.requested_netlist, agentlib::access_type::read,
		                                    args_.safe_netlist, sec_err)) {
			out_error = sec_err;
			return false;
		}
	}

	if (raw_json.contains("target") && raw_json["target"].is_string()) {
		args_.target_mode = raw_json["target"].get<std::string>();
	}

	if (raw_json.contains("top_k") && raw_json["top_k"].is_number_integer()) {
		int k = raw_json["top_k"].get<int>();
		args_.top_k = (k > 0) ? (size_t)k : 3;
	}

	if (raw_json.contains("write_sidecar") && raw_json["write_sidecar"].is_boolean()) {
		args_.write_sidecar = raw_json["write_sidecar"].get<bool>();
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> verilog_logic_depth_validator::create_tool_impl(const nlohmann::json &/*raw_json*/) const
{
	return std::make_unique<verilog_logic_depth_tool>(args_);
}

extern "C" {

void register_verilog_logic_depth(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::verilog_logic_depth_validator>(); }
	);
}

void unregister_verilog_logic_depth(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("verilog_logic_depth");
}

}

} // namespace tools
