#include "plugins/verilog/netlist_graph_analyzer.h"
#include "plugins/verilog/nextpnr_timing_analyzer.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "fs_utils.h"
#include "markdown_utils.h"

#include <CLI11.hpp>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string format_for_terminal(const std::string &md, bool framed_tables)
{
	if (!framed_tables) {
		return markdown_utils::align_all_tables(md, false);
	}
	// Strip inline markdown markers (` and **) inside table rows so terminal column widths match visual width
	std::string prepped;
	prepped.reserve(md.size());
	size_t pos = 0;
	while (pos < md.size()) {
		size_t nl = md.find('\n', pos);
		std::string_view line = (nl == std::string::npos)
			? std::string_view(md).substr(pos)
			: std::string_view(md).substr(pos, nl - pos);
		std::string_view trimmed = line;
		while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
			trimmed.remove_prefix(1);
		}
		if (!trimmed.empty() && trimmed.front() == '|') {
			for (size_t i = 0; i < line.size(); ++i) {
				if (line[i] == '`') {
					continue;
				}
				if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*') {
					++i;
					continue;
				}
				// Replace UTF-8 en-dash/em-dash inside table cells with ASCII '-' so byte-width padding aligns visually
				if (i + 2 < line.size() &&
				    static_cast<unsigned char>(line[i]) == 0xE2 &&
				    static_cast<unsigned char>(line[i + 1]) == 0x80 &&
				    (static_cast<unsigned char>(line[i + 2]) == 0x93 || static_cast<unsigned char>(line[i + 2]) == 0x94)) {
					prepped.push_back('-');
					i += 2;
					continue;
				}
				prepped.push_back(line[i]);
			}
		} else {
			for (size_t i = 0; i < line.size(); ++i) {
				if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*') {
					++i;
					continue;
				}
				prepped.push_back(line[i]);
			}
		}
		if (nl != std::string::npos) {
			prepped.push_back('\n');
			pos = nl + 1;
		} else {
			break;
		}
	}
	return markdown_utils::align_all_tables(prepped, true);
}

int main(int argc, char **argv)
{
	CLI::App app{
		"TurboStar Verilog Timing & PnR Analyzer (turbostar-verilog-timing)\n"
		"  Default mode : Analyze pre-PnR logic depth, critical paths & area for a module (verilog_logic_depth)\n"
		"  --pnr        : Run physical place-and-route timing closure analysis (verilog_pnr_timing)\n"
		"  -u, --update : Batch-synthesize all modules and update <module>.timing.md sidecars (verilog_update_timing)"
	};

	bool mode_pnr = false;
	bool mode_update = false;

	std::string untrusted_path;
	std::string untrusted_module;
	std::string untrusted_target;
	std::string untrusted_netlist;
	std::string untrusted_timing_report;

	size_t top_k = 3;
	int show_code = 1;
	int seeds = 1;
	bool show_segments = false;
	bool all_paths = false;
	bool no_group = false;
	bool no_collapse = false;
	bool no_frames = false;

	bool dry_run = false;
	bool quiet = false;
	bool verbose = false;

	app.add_flag("--pnr", mode_pnr, "Run physical place-and-route timing analysis (nextpnr)");
	app.add_flag("-u,--update", mode_update, "Batch-synthesize all workspace modules and update .timing.md sidecars");

	app.add_option("path,-p,--path", untrusted_path, "Target directory, RTL source file, or module name (default: from turbostar.json or '.')");
	app.add_option("-m,--module", untrusted_module, "Target RTL module name or source file (e.g. 'alu8', 'cpu', 'src/alu8.sv')");
	app.add_option("-t,--target", untrusted_target, "Synthesis target ('Generic LUT4', 'Generic LUT6', 'iCE40', 'Generic Gates')");
	app.add_option("-n,--netlist", untrusted_netlist, "Path to pre-synthesized Yosys JSON netlist file (optional)");
	app.add_option("--timing-report", untrusted_timing_report, "Path to pre-generated nextpnr timing JSON report (implies --pnr)");

	app.add_option("-k,--top-k", top_k, "Maximum number of critical paths/bottlenecks to display (default: 3)");
	app.add_option("-c,--show-code", show_code, "RTL source lines per hop: 1=inline snippet (default), >1=multi-line block, 0=off");
	app.add_option("--seeds", seeds, "Number of placement seeds in --pnr mode (default: 1)");
	app.add_flag("--segments", show_segments, "Show full hop-by-hop cell/pin/net segments in --pnr mode");
	app.add_flag("--all-paths", all_paths, "Include async I/O pad paths in --pnr mode (default: Reg -> Reg only)");
	app.add_flag("--no-group", no_group, "Do not group converging paths by sink endpoint");
	app.add_flag("--no-collapse", no_collapse, "Do not collapse multi-hop carry/LUT stages");
	app.add_flag("--no-frames", no_frames, "Use plain pipe Markdown tables instead of UTF-8 box-drawing frames");

	app.add_flag("-d,--dry-run", dry_run, "Dry run in --update mode: analyze all modules without writing .timing.md files");
	app.add_flag("-q,--quiet", quiet, "Quiet mode in --update mode: suppress progress output");
	app.add_flag("-v,--verbose", verbose, "Verbose mode: print synthesis failure logs (or enable --segments in --pnr mode)");

	CLI11_PARSE(app, argc, argv);
	const bool framed_tables = !no_frames;

	std::string prog_name = (argc > 0 && argv[0]) ? fs::path(argv[0]).filename().string() : "";
	if (prog_name.find("update-timing") != std::string::npos || dry_run) {
		mode_update = true;
	}
	if (!untrusted_timing_report.empty()) {
		mode_pnr = true;
	}

	if (show_code < 0) {
		show_code = 0;
	} else if (show_code > 50) {
		show_code = 50;
	}
	if (top_k == 0) {
		top_k = 3;
	}

	std::error_code ec;
	std::string cwd_str = fs::current_path(ec).string();
	if (cwd_str.empty()) {
		cwd_str = ".";
	}

	// Load FPGA project settings from current directory if present
	verilog::fpga_target_config cfg;
	std::string cfg_err;
	bool has_cfg = verilog::fpga_project_settings::load_from_project(cwd_str, cfg, cfg_err);

	if (untrusted_target.empty()) {
		if (has_cfg && cfg.target == "ice40") {
			untrusted_target = "iCE40";
		} else {
			untrusted_target = "Generic LUT4";
		}
	}

	std::string safe_target = untrusted_target;
	if (safe_target != "Generic LUT4" && safe_target != "Generic LUT6" &&
	    safe_target != "iCE40" && safe_target != "Generic Gates") {
		std::cerr << std::format("Error: Unsupported target '{}'. Allowed: 'Generic LUT4', 'Generic LUT6', 'iCE40', 'Generic Gates'\n",
		                         safe_target);
		return 1;
	}

	std::string safe_netlist;
	if (!untrusted_netlist.empty()) {
		fs::path netlist_fs_path(untrusted_netlist);
		if (!fs::is_regular_file(netlist_fs_path, ec)) {
			std::cerr << std::format("Error: Netlist file '{}' does not exist or is not a regular file.\n", untrusted_netlist);
			return 1;
		}
		safe_netlist = fs::weakly_canonical(netlist_fs_path, ec).string();
	}

	std::string safe_timing_report;
	if (!untrusted_timing_report.empty()) {
		fs::path tr_fs_path(untrusted_timing_report);
		if (!fs::is_regular_file(tr_fs_path, ec)) {
			std::cerr << std::format("Error: Timing report '{}' does not exist or is not a regular file.\n", untrusted_timing_report);
			return 1;
		}
		safe_timing_report = fs::weakly_canonical(tr_fs_path, ec).string();
	}

	std::string clean_module;
	if (!untrusted_module.empty()) {
		if (!fs_utils::is_safe_for_ui(untrusted_module)) {
			std::cerr << "Error: Invalid characters in --module argument.\n";
			return 1;
		}
		clean_module = untrusted_module;
	}

	// If positional 'path' was given and is not an existing directory in non-update mode,
	// check if it's a module name or a single .sv/.v file
	std::string safe_file;
	if (!untrusted_path.empty()) {
		fs::path p_cand(untrusted_path);
		if (fs::exists(p_cand, ec)) {
			if (fs::is_regular_file(p_cand, ec)) {
				safe_file = fs::weakly_canonical(p_cand, ec).string();
			}
		} else if (!mode_update && clean_module.empty()) {
			if (!fs_utils::is_safe_for_ui(untrusted_path)) {
				std::cerr << "Error: Invalid characters in path/module argument.\n";
				return 1;
			}
			clean_module = untrusted_path;
			untrusted_path.clear();
		} else {
			std::cerr << std::format("Error: Path '{}' does not exist.\n", untrusted_path);
			return 1;
		}
	}

	if (safe_file.empty() && !clean_module.empty()) {
		fs::path m_cand(clean_module);
		if (fs::exists(m_cand, ec) && fs::is_regular_file(m_cand, ec)) {
			safe_file = fs::weakly_canonical(m_cand, ec).string();
		}
	}

	// =========================================================================
	// MODE 1: Physical Place-and-Route Timing (--pnr)
	// =========================================================================
	if (mode_pnr) {
		verilog::nextpnr_timing_analyzer pnr_analyzer;
		std::string err;

		if (!safe_timing_report.empty()) {
			if (!pnr_analyzer.load_from_files(safe_timing_report, safe_netlist, err)) {
				std::cerr << std::format("Error loading timing report '{}': {}\n", safe_timing_report, err);
				return 1;
			}
			std::string md = pnr_analyzer.generate_timing_markdown(
				!all_paths, !no_group, top_k, !no_collapse, show_segments || verbose, show_code);
			std::cout << format_for_terminal(md, framed_tables);
			return 0;
		}

		auto sys_tools = verilog::fpga_project_settings::detect_system_tools();
		auto pcfs = verilog::fpga_project_settings::scan_pcf_files(cwd_str);
		if (cfg.target.empty() && sys_tools.available_targets.size() == 1) {
			cfg.target = sys_tools.available_targets.front();
		}
		std::string configured_top = cfg.top_module;
		if (!clean_module.empty() && cfg.top_module.empty()) {
			cfg.top_module = clean_module;
		}

		std::vector<std::string> missing;
		if (!verilog::fpga_project_settings::is_complete(cfg, missing)) {
			std::cout << format_for_terminal(
				verilog::fpga_project_settings::format_guidance(cwd_str, cfg, sys_tools, pcfs), framed_tables);
			return 1;
		}
		if (cfg.pcf_file.empty() && pcfs.size() == 1) {
			cfg.pcf_file = pcfs.front();
		}

		fs::path scan_root = cwd_str;
		if (!untrusted_path.empty() && fs::is_directory(untrusted_path, ec)) {
			scan_root = fs::weakly_canonical(untrusted_path, ec);
		} else if (!safe_file.empty()) {
			scan_root = fs::path(safe_file).parent_path();
			std::string fname = scan_root.filename().string();
			if (scan_root.has_parent_path() && fname != "sv" && fname != "src" && fname != "rtl" &&
			    (cfg.sv_path.empty() || fname != cfg.sv_path) && !fs::equivalent(scan_root, fs::path(cwd_str), ec)) {
				scan_root = scan_root.parent_path();
			}
		} else if (!cfg.sv_path.empty()) {
			scan_root = fs::path(cwd_str) / cfg.sv_path;
		}

		std::vector<std::string> sources = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);
		if (sources.empty() && !safe_file.empty()) {
			sources.push_back(safe_file);
		}
		if (sources.empty()) {
			std::cerr << "Error: No synthesizable SystemVerilog/Verilog source files found.\n";
			return 1;
		}

		std::string submodule_banner;
		if (!clean_module.empty()) {
			std::string resolved_mod = verilog::verilog_module_scanner::resolve_module_name(clean_module, safe_file, sources);
			if (!resolved_mod.empty()) {
				cfg.top_module = resolved_mod;
				if (!configured_top.empty() && resolved_mod != configured_top) {
					cfg.pcf_file.clear();
					submodule_banner = std::format(
						"> [!NOTE] **Out-of-Context Submodule PnR (`{}`)**: Analyzed submodule `{}` with unconstrained I/O placement (configured project `top_module` is `{}`).\n\n",
						resolved_mod, resolved_mod, configured_top);
				}
			}
		}

		if (!pnr_analyzer.synthesize_and_route(cfg, sources, err)) {
			std::cerr << std::format("On-demand synthesis and place-and-route failed: {}\n", err);
			return 1;
		}

		std::string md = submodule_banner + pnr_analyzer.generate_timing_markdown(
			!all_paths, !no_group, top_k, !no_collapse, show_segments || verbose, show_code);
		std::cout << format_for_terminal(md, framed_tables);
		return 0;
	}

	// Gather synthesizable RTL files for Mode 2 (--update) and Mode 3 (default logic_depth)
	if (untrusted_path.empty()) {
		if (has_cfg && !cfg.sv_path.empty()) {
			untrusted_path = cfg.sv_path;
		} else {
			untrusted_path = ".";
		}
	}

	fs::path input_fs_path(untrusted_path);
	if (!fs::exists(input_fs_path, ec)) {
		std::cerr << std::format("Error: Path '{}' does not exist.\n", untrusted_path);
		return 1;
	}
	std::string safe_path = fs::weakly_canonical(input_fs_path, ec).string();

	std::vector<std::string> sv_files;
	try {
		if (!safe_file.empty() && !mode_update) {
			fs::path scan_root = fs::path(safe_file).parent_path();
			std::string fname = scan_root.filename().string();
			if (scan_root.has_parent_path() && fname != "sv" && fname != "src" && fname != "rtl" &&
			    (cfg.sv_path.empty() || fname != cfg.sv_path) && !fs::equivalent(scan_root, fs::path(cwd_str), ec)) {
				scan_root = scan_root.parent_path();
			}
			sv_files = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);
			if (sv_files.empty()) {
				sv_files.push_back(safe_file);
			}
		} else {
			sv_files = verilog::verilog_module_scanner::find_synthesizable_files(safe_path);
		}
	} catch (const std::exception &e) {
		std::cerr << std::format("Error scanning path '{}': {}\n", safe_path, e.what());
		return 1;
	}

	if (sv_files.empty() && safe_netlist.empty()) {
		std::cerr << std::format("Error: No synthesizable Verilog/SystemVerilog files found in '{}'\n", safe_path);
		return 1;
	}

	// =========================================================================
	// MODE 2: Batch Update Sidecars (-u / --update)
	// =========================================================================
	if (mode_update) {
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
			size_t luts{0};
			size_t ffs{0};
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
				const auto &area = analyzer.get_area_summary();
				res.luts = (safe_target == "Generic Gates") ? (area.other_comb_count + area.lut_count) : area.lut_count;
				res.ffs = area.ff_count;
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
			std::string table_md = "| Module | Reg->Reg Depth | LUTs | FFs | Inputs | Outputs | Status | Sidecar Path |\n"
			                       "| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |\n";

			for (const auto &r : results) {
				std::string depth_str = r.success ? std::format("{} LUTs", r.reg_depth) : "-";
				std::string lut_str = r.success ? std::format("{}", r.luts) : "-";
				std::string ff_str = r.success ? std::format("{}", r.ffs) : "-";
				std::string in_str = r.success ? std::format("{}", r.inputs) : "-";
				std::string out_str = r.success ? std::format("{}", r.outputs) : "-";
				std::string status_str = r.success ? (dry_run ? "DRY-RUN" : "OK") : "FAILED";
				std::string rel_sidecar = verilog::nextpnr_timing_analyzer::clean_display_location(r.sidecar_path, cwd_str);

				table_md += std::format("| {} | {} | {} | {} | {} | {} | {} | {} |\n",
				                        r.module_name, depth_str, lut_str, ff_str, in_str, out_str, status_str, rel_sidecar);
			}
			std::cout << "\n" << format_for_terminal(table_md, framed_tables) << "\n";
			std::cout << std::format("{}: {}/{} modules processed (Target: {})\n",
			                         dry_run ? "Dry run completed" : "Summary",
			                         success_count, results.size(), safe_target);
		}
		return (success_count == results.size()) ? 0 : 1;
	}

	// =========================================================================
	// MODE 3 (Default): Single-Module Logic Depth & Area Report
	// =========================================================================
	verilog::netlist_graph_analyzer analyzer;
	std::string err;

	if (!safe_netlist.empty()) {
		if (!analyzer.load_from_file(safe_netlist, err, clean_module)) {
			std::cerr << std::format("Error loading netlist '{}': {}\n", safe_netlist, err);
			return 1;
		}
	} else {
		std::string resolved_module = verilog::verilog_module_scanner::resolve_module_name(
			clean_module, safe_file, sv_files);
		if (resolved_module.empty()) {
			if (!cfg.top_module.empty()) {
				resolved_module = cfg.top_module;
			} else {
				auto discovered = verilog::verilog_module_scanner::discover_modules_in_files(sv_files);
				if (discovered.size() == 1) {
					resolved_module = discovered.front().module_name;
				} else {
					std::string mod_list;
					for (size_t i = 0; i < discovered.size(); ++i) {
						if (i > 0) mod_list += ", ";
						mod_list += std::format("{} ({})", discovered[i].module_name,
						                        fs::path(discovered[i].source_file).filename().string());
					}
					std::cerr << "Error: Multiple modules found and no module specified (nor top_module in turbostar.json).\n"
					          << "Pass -m <module> (or -u to batch-update all .timing.md sidecars).\n";
					if (!mod_list.empty()) {
						std::cerr << std::format("Available modules: {}\n", mod_list);
					}
					return 1;
				}
			}
		}

		if (!analyzer.synthesize_and_load(resolved_module, sv_files, safe_target, err)) {
			std::cerr << std::format("Synthesis/analysis failed for module '{}': {}\n", resolved_module, err);
			return 1;
		}
	}

	std::string base_dir = cwd_str;
	if (!safe_file.empty()) {
		fs::path sdir = fs::path(safe_file).parent_path();
		if ((sdir.filename() == "sv" || sdir.filename() == "src" || sdir.filename() == "rtl") && sdir.has_parent_path()) {
			sdir = sdir.parent_path();
		}
		base_dir = sdir.string();
	} else if (!safe_netlist.empty()) {
		fs::path ndir = fs::path(safe_netlist).parent_path();
		if ((ndir.filename() == "synth" || ndir.filename() == "build") && ndir.has_parent_path()) {
			ndir = ndir.parent_path();
		}
		base_dir = ndir.string();
	} else if (fs::is_directory(safe_path, ec)) {
		fs::path pdir(safe_path);
		if ((pdir.filename() == "sv" || pdir.filename() == "src" || pdir.filename() == "rtl") && pdir.has_parent_path()) {
			pdir = pdir.parent_path();
		}
		base_dir = pdir.string();
	}

	std::string report_md = analyzer.generate_logic_depth_report_markdown(
		safe_target, top_k, !no_group, !no_collapse, show_code, base_dir);
	std::cout << format_for_terminal(report_md, framed_tables);
	return 0;
}
