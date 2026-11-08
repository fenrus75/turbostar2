#include "plugins/verilog/verilog_update_timing_tool.h"
#include "plugins/verilog/netlist_graph_analyzer.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include "fs_utils.h"

#include <filesystem>
#include <fstream>
#include <format>
#include <vector>

namespace tools
{

verilog_update_timing_tool::verilog_update_timing_tool(verilog_update_timing_args args)
	: agentlib::llm_tool_action(std::format("Updating timing sidecars in {}", args.requested_path)),
	  args_(std::move(args))
{
}

bool verilog_update_timing_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string verilog_update_timing_tool::execute(agentlib::tool_context &ctx)
{
	std::vector<std::string> sv_files;
	try {
		sv_files = verilog::verilog_module_scanner::find_synthesizable_files(args_.safe_path);
	} catch (const std::exception &e) {
		set_failure(ctx, e.what());
		return std::format("Error scanning directory '{}': {}", args_.safe_path, e.what());
	}

	if (sv_files.empty()) {
		set_failure(ctx);
		return std::format("No Verilog/SystemVerilog files found in '{}'", args_.safe_path);
	}

	auto declared_modules = verilog::verilog_module_scanner::discover_modules_in_files(sv_files);
	if (declared_modules.empty()) {
		set_failure(ctx);
		return std::format("No module declarations found in {} scanned files in '{}'", sv_files.size(), args_.safe_path);
	}

	struct update_stat {
		std::string module_name;
		std::string sidecar_path;
		size_t reg_depth{0};
		size_t inputs{0};
		size_t outputs{0};
		bool success{false};
		std::string err;
	};

	std::vector<update_stat> stats;

	for (const auto &decl : declared_modules) {
		verilog::netlist_graph_analyzer analyzer;
		std::string err;
		bool loaded = false;

		if (!args_.safe_netlist.empty()) {
			loaded = analyzer.load_from_file(args_.safe_netlist, err, decl.module_name);
		} else {
			loaded = analyzer.synthesize_and_load(decl.module_name, sv_files, args_.target_mode, err);
		}

		update_stat st;
		st.module_name = decl.module_name;
		st.sidecar_path = decl.sidecar_path;

		if (!loaded) {
			st.success = false;
			st.err = err;
		} else {
			st.reg_depth = analyzer.get_max_reg_to_reg_depth();
			st.inputs = analyzer.get_input_ports().size();
			st.outputs = analyzer.get_output_ports().size();

			std::string md_content = analyzer.generate_timing_markdown(args_.target_mode);

			std::string safe_out_path;
			std::string sec_err;
			if (ctx.fs_security.validate_access(st.sidecar_path, agentlib::access_type::write, safe_out_path, sec_err)) {
				std::ofstream ofs(safe_out_path);
				if (ofs.is_open()) {
					ofs << md_content;
					st.success = true;
				} else {
					st.success = false;
					st.err = "Failed to open file for writing";
				}
			} else {
				st.success = false;
				st.err = sec_err;
			}
		}

		stats.push_back(std::move(st));
	}

	size_t success_count = 0;
	for (const auto &st : stats) {
		if (st.success) {
			success_count++;
		}
	}

	std::string report = std::format("### Timing Sidecars Updated ({}/{} modules)\n\n", success_count, stats.size());
	report += std::format("Target: {}\n\n", args_.target_mode);
	report += "| Module | Sidecar Path | Reg -> Reg Depth | Inputs | Outputs | Status |\n";
	report += "| :--- | :--- | :---: | :---: | :---: | :--- |\n";

	for (const auto &st : stats) {
		if (st.success) {
			report += std::format("| `{}` | `{}` | {} | {} | {} | OK |\n",
			                      st.module_name, st.sidecar_path, st.reg_depth, st.inputs, st.outputs);
		} else {
			report += std::format("| `{}` | `{}` | - | - | - | Failed ({}) |\n",
			                      st.module_name, st.sidecar_path, st.err);
		}
	}

	if (success_count > 0) {
		set_success(ctx);
	} else {
		set_failure(ctx, "Failed to update any timing sidecars");
	}

	return report;
}

} // namespace tools
