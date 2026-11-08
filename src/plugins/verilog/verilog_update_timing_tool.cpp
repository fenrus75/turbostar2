#include "plugins/verilog/verilog_update_timing_tool.h"
#include "plugins/verilog/netlist_graph_analyzer.h"
#include "fs_utils.h"

#include <ctre.hpp>
#include <filesystem>
#include <fstream>
#include <format>
#include <vector>

namespace tools
{

struct module_discovery_entry {
	std::string module_name;
	std::string source_file;
};

static std::vector<module_discovery_entry> discover_modules_in_files(const std::vector<std::string> &files)
{
	std::vector<module_discovery_entry> results;
	for (const auto &fpath : files) {
		std::ifstream ifs(fpath);
		if (!ifs.is_open()) {
			continue;
		}

		std::string line;
		while (std::getline(ifs, line)) {
			// Strip single line comments
			auto cpos = line.find("//");
			if (cpos != std::string::npos) {
				line.resize(cpos);
			}

			// Search for module <name>
			if (auto m = ctre::search<"(?:^|\\s)module\\s+([a-zA-Z_0-9]+)">(line)) {
				std::string mod_name = m.get<1>().to_string();
				results.push_back({std::move(mod_name), fpath});
			}
		}
	}
	return results;
}

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
		std::filesystem::path root(args_.safe_path);
		if (std::filesystem::is_regular_file(root)) {
			sv_files.push_back(args_.safe_path);
		} else if (std::filesystem::is_directory(root)) {
			for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
				if (entry.is_regular_file()) {
					auto ext = entry.path().extension().string();
					if (ext == ".sv" || ext == ".v") {
						sv_files.push_back(entry.path().string());
					}
				}
			}
		}
	} catch (const std::exception &e) {
		set_failure(ctx, e.what());
		return std::format("Error scanning directory '{}': {}", args_.safe_path, e.what());
	}

	if (sv_files.empty()) {
		set_failure(ctx);
		return std::format("No Verilog/SystemVerilog files found in '{}'", args_.safe_path);
	}

	auto declared_modules = discover_modules_in_files(sv_files);
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

		// Sidecar path in same directory as declaration
		std::filesystem::path src_p(decl.source_file);
		std::filesystem::path sidecar_p = src_p.parent_path() / (decl.module_name + ".timing.md");
		st.sidecar_path = sidecar_p.string();

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
