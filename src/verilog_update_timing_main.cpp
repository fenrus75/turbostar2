#include "plugins/verilog/netlist_graph_analyzer.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include "fs_utils.h"

#include <CLI11.hpp>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
	CLI::App app{"TurboStar Verilog Timing Sidecar Updater - synthesizes and updates module .timing.md sidecars"};

	std::string untrusted_path = ".";
	std::string untrusted_target = "Generic LUT4";
	std::string untrusted_netlist;
	bool dry_run = false;
	bool quiet = false;
	bool verbose = false;

	app.add_option("path,-p,--path", untrusted_path, "Root directory or file to scan for Verilog modules (default: '.')");
	app.add_option("-t,--target", untrusted_target, "Synthesis target ('Generic LUT4', 'Generic LUT6', 'iCE40', 'Generic Gates') (default: 'Generic LUT4')");
	app.add_option("-n,--netlist", untrusted_netlist, "Path to pre-synthesized JSON netlist file (optional)");
	app.add_flag("-d,--dry-run", dry_run, "Dry run: analyze without writing sidecar files");
	app.add_flag("-q,--quiet", quiet, "Quiet mode: suppress informational output");
	app.add_flag("-v,--verbose", verbose, "Verbose mode: print detailed analysis and synthesis log on failure");

	CLI11_PARSE(app, argc, argv);

	// Validate target mode
	std::string safe_target = untrusted_target;
	if (safe_target != "Generic LUT4" && safe_target != "Generic LUT6" &&
	    safe_target != "iCE40" && safe_target != "Generic Gates") {
		std::cerr << std::format("Error: Unsupported target '{}'. Allowed: 'Generic LUT4', 'Generic LUT6', 'iCE40', 'Generic Gates'\n",
		                         safe_target);
		return 1;
	}

	// Validate input path
	std::filesystem::path input_fs_path(untrusted_path);
	std::error_code ec;
	if (!std::filesystem::exists(input_fs_path, ec)) {
		std::cerr << std::format("Error: Path '{}' does not exist.\n", untrusted_path);
		return 1;
	}
	std::string safe_path = std::filesystem::weakly_canonical(input_fs_path, ec).string();

	std::string safe_netlist;
	if (!untrusted_netlist.empty()) {
		std::filesystem::path netlist_fs_path(untrusted_netlist);
		if (!std::filesystem::is_regular_file(netlist_fs_path, ec)) {
			std::cerr << std::format("Error: Netlist file '{}' does not exist or is not a regular file.\n", untrusted_netlist);
			return 1;
		}
		safe_netlist = std::filesystem::weakly_canonical(netlist_fs_path, ec).string();
	}

	std::vector<std::string> sv_files;
	try {
		sv_files = verilog::verilog_module_scanner::find_synthesizable_files(safe_path);
	} catch (const std::exception &e) {
		std::cerr << std::format("Error scanning path '{}': {}\n", safe_path, e.what());
		return 1;
	}

	if (sv_files.empty()) {
		std::cerr << std::format("Error: No synthesizable Verilog/SystemVerilog files found in '{}'\n", safe_path);
		return 1;
	}

	auto declared_modules = verilog::verilog_module_scanner::discover_modules_in_files(sv_files);
	if (declared_modules.empty()) {
		std::cerr << std::format("Error: No module declarations found across {} scanned files in '{}'\n",
		                         sv_files.size(), safe_path);
		return 1;
	}

	struct module_result {
		std::string module_name;
		std::string sidecar_path;
		size_t reg_depth{0};
		size_t inputs{0};
		size_t outputs{0};
		bool success{false};
		std::string err;
	};

	std::vector<module_result> results;
	size_t current_idx = 0;

	for (const auto &decl : declared_modules) {
		++current_idx;
		if (!quiet) {
			std::cout << std::format("[{}/{}] Analyzing module '{}'...\n", current_idx, declared_modules.size(), decl.module_name);
		}

		verilog::netlist_graph_analyzer analyzer;
		std::string err;
		bool loaded = false;

		if (!safe_netlist.empty()) {
			loaded = analyzer.load_from_file(safe_netlist, err, decl.module_name);
		} else {
			loaded = analyzer.synthesize_and_load(decl.module_name, sv_files, safe_target, err);
		}

		module_result res;
		res.module_name = decl.module_name;
		res.sidecar_path = decl.sidecar_path;

		if (!loaded) {
			res.success = false;
			res.err = err;
			if (verbose) {
				std::cerr << std::format("  Failed to synthesize module '{}': {}\n", decl.module_name, err);
			}
		} else {
			res.reg_depth = analyzer.get_max_reg_to_reg_depth();
			res.inputs = analyzer.get_input_ports().size();
			res.outputs = analyzer.get_output_ports().size();

			if (dry_run) {
				res.success = true;
			} else {
				std::string md_content = analyzer.generate_timing_markdown(safe_target);
				std::ofstream ofs(res.sidecar_path);
				if (ofs.is_open()) {
					ofs << md_content;
					res.success = true;
				} else {
					res.success = false;
					res.err = "Failed to open sidecar file for writing";
				}
			}
		}

		results.push_back(std::move(res));
	}

	size_t success_count = 0;
	for (const auto &r : results) {
		if (r.success) {
			++success_count;
		}
	}

	if (!quiet) {
		std::cout << "\n";
		std::cout << std::format("{:<20} | {:<16} | {:<7} | {:<8} | {:<8} | {}\n",
		                         "Module", "Reg->Reg Depth", "Inputs", "Outputs", "Status", "Sidecar Path");
		std::cout << std::string(90, '-') << "\n";

		for (const auto &r : results) {
			std::string depth_str = r.success ? std::format("{} LUTs", r.reg_depth) : "-";
			std::string in_str = r.success ? std::format("{}", r.inputs) : "-";
			std::string out_str = r.success ? std::format("{}", r.outputs) : "-";
			std::string status_str;
			if (r.success) {
				status_str = dry_run ? "DRY-RUN" : "OK";
			} else {
				status_str = "FAILED";
			}

			std::cout << std::format("{:<20} | {:<16} | {:<7} | {:<8} | {:<8} | {}\n",
			                         r.module_name, depth_str, in_str, out_str, status_str, r.sidecar_path);
		}
		std::cout << "\n";
		std::cout << std::format("{}: {}/{} modules processed (Target: {})\n",
		                         dry_run ? "Dry run completed" : "Summary",
		                         success_count, results.size(), safe_target);
	}

	return (success_count == results.size()) ? 0 : 1;
}
