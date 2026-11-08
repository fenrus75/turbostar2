#include "plugins/verilog/verilog_update_timing_tool.h"
#include "plugins/verilog/netlist_graph_analyzer.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include "plugins/verilog/fpga_project_settings.h"
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
	std::string project_root = ctx.fs_security.get_working_directory().string();
	if (project_root.empty()) {
		project_root = ".";
	}

	verilog::fpga_target_config cfg;
	std::string load_err;
	verilog::fpga_project_settings::load_from_project(project_root, cfg, load_err);

	std::string scan_dir = args_.safe_path;
	if ((args_.requested_path.empty() || args_.requested_path == ".") && !cfg.sv_path.empty()) {
		std::string cand = (std::filesystem::path(project_root) / cfg.sv_path).string();
		std::string safe_cand;
		std::string sec_err;
		if (ctx.fs_security.validate_access(cand, agentlib::access_type::read, safe_cand, sec_err)) {
			scan_dir = safe_cand;
		}
	}

	std::string target_mode = args_.target_mode;
	if (target_mode == "Generic LUT4" && cfg.target == "ice40") {
		target_mode = "iCE40";
	}

	std::vector<std::string> sv_files;
	try {
		sv_files = verilog::verilog_module_scanner::find_synthesizable_files(scan_dir);
	} catch (const std::exception &e) {
		set_failure(ctx, e.what());
		return std::format("Error scanning directory '{}': {}", scan_dir, e.what());
	}

	if (sv_files.empty()) {
		set_failure(ctx);
		return std::format("No Verilog/SystemVerilog files found in '{}'", scan_dir);
	}

	auto declared_modules = verilog::verilog_module_scanner::discover_modules_in_files(sv_files);
	if (declared_modules.empty()) {
		set_failure(ctx);
		return std::format("No module declarations found in {} scanned files in '{}'", sv_files.size(), scan_dir);
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
			loaded = analyzer.synthesize_and_load(decl.module_name, sv_files, target_mode, err);
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

			std::string md_content = analyzer.generate_timing_markdown(target_mode);

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
