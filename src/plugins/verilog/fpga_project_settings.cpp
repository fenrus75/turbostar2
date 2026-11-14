#include "plugins/verilog/fpga_project_settings.h"
#include "fs_utils.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace verilog {

namespace fs = std::filesystem;

namespace {
bool g_is_cli_mode = false;
} // namespace

void fpga_project_settings::set_cli_mode(bool cli) noexcept
{
	g_is_cli_mode = cli;
}

bool fpga_project_settings::is_cli_mode() noexcept
{
	return g_is_cli_mode;
}

fpga_system_tools fpga_project_settings::detect_system_tools()
{
	fpga_system_tools tools;
	tools.yosys_path = fs_utils::find_executable("yosys");
	tools.nextpnr_ice40_path = fs_utils::find_executable("nextpnr-ice40");
	tools.nextpnr_generic_path = fs_utils::find_executable("nextpnr-generic");
	tools.nextpnr_ecp5_path = fs_utils::find_executable("nextpnr-ecp5");

	if (!tools.nextpnr_ice40_path.empty()) {
		tools.available_targets.push_back("ice40");
	}
	if (!tools.nextpnr_generic_path.empty()) {
		tools.available_targets.push_back("generic");
	}
	if (!tools.nextpnr_ecp5_path.empty()) {
		tools.available_targets.push_back("ecp5");
	}
	return tools;
}

std::string fpga_project_settings::detect_sv_path(std::string_view project_root)
{
	fs::path root(project_root.empty() ? "." : project_root);
	std::error_code ec;

	auto has_verilog_files = [](const fs::path &dir) -> bool {
		std::error_code ec;
		if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return false;
		for (const auto &entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
			if (entry.is_regular_file(ec)) {
				auto ext = entry.path().extension();
				if (ext == ".sv" || ext == ".v") return true;
			}
		}
		return false;
	};

	for (const auto &cand : {"sv", "rtl", "src"}) {
		fs::path p = root / cand;
		if (has_verilog_files(p)) {
			return cand;
		}
	}
	return "";
}

std::vector<std::string> fpga_project_settings::scan_pcf_files(std::string_view project_root)
{
	std::vector<std::string> pcf_files;
	if (project_root.empty()) {
		project_root = ".";
	}

	std::error_code ec;
	fs::path root_path(project_root);
	if (!fs::exists(root_path, ec) || !fs::is_directory(root_path, ec)) {
		return pcf_files;
	}

	try {
		for (const auto &entry : fs::recursive_directory_iterator(root_path, fs::directory_options::skip_permission_denied, ec)) {
			if (entry.is_regular_file(ec) && entry.path().extension() == ".pcf") {
				// Store relative path to project_root if possible
				auto rel = fs::relative(entry.path(), root_path, ec);
				if (!ec) {
					pcf_files.push_back(rel.string());
				} else {
					pcf_files.push_back(entry.path().string());
				}
			}
		}
	} catch (...) {
	}

	std::sort(pcf_files.begin(), pcf_files.end());
	return pcf_files;
}

bool fpga_project_settings::load_from_project(std::string_view project_root, fpga_target_config &out_cfg, std::string &out_error)
{
	fs::path root(project_root.empty() ? "." : project_root);
	std::vector<std::string> candidates = {"turbostar.json", "project.json"};

	fs::path found_path;
	for (const auto &cand : candidates) {
		fs::path p = root / cand;
		std::error_code ec;
		if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
			found_path = p;
			break;
		}
	}

	if (found_path.empty()) {
		out_error = "No turbostar.json or project.json found in project root";
		return false;
	}

	std::ifstream f(found_path);
	if (!f.is_open()) {
		out_error = std::format("Failed to open config file '{}'", found_path.string());
		return false;
	}

	nlohmann::json j;
	try {
		f >> j;
	} catch (const std::exception &e) {
		out_error = std::format("JSON parse error in '{}': {}", found_path.string(), e.what());
		return false;
	}

	out_cfg.active_config_file = found_path.string();

	const nlohmann::json *fpga_ptr = nullptr;
	if (j.contains("fpga") && j["fpga"].is_object()) {
		fpga_ptr = &j["fpga"];
	} else if (j.contains("fpga_target") && j["fpga_target"].is_object()) {
		fpga_ptr = &j["fpga_target"];
	} else {
		out_error = std::format("Config file '{}' does not contain an 'fpga' configuration object", found_path.string());
		return false;
	}

	const auto &fpga = *fpga_ptr;
	out_cfg.target = fpga.value("target", "");
	out_cfg.device = fpga.value("device", "");
	out_cfg.package = fpga.value("package", "");
	if (fpga.contains("top_module")) {
		out_cfg.top_module = fpga.value("top_module", "");
	} else if (fpga.contains("top")) {
		out_cfg.top_module = fpga.value("top", "");
	}

	if (fpga.contains("freq_mhz")) {
		out_cfg.freq_mhz = fpga.value("freq_mhz", 0.0);
	} else if (fpga.contains("freq")) {
		out_cfg.freq_mhz = fpga.value("freq", 0.0);
	}

	if (fpga.contains("pcf_file")) {
		out_cfg.pcf_file = fpga.value("pcf_file", "");
	} else if (fpga.contains("pcf")) {
		out_cfg.pcf_file = fpga.value("pcf", "");
	}

	if (fpga.contains("sv_path")) {
		out_cfg.sv_path = fpga.value("sv_path", "");
	} else if (fpga.contains("default_sv_path")) {
		out_cfg.sv_path = fpga.value("default_sv_path", "");
	} else if (fpga.contains("source_dir")) {
		out_cfg.sv_path = fpga.value("source_dir", "");
	} else if (fpga.contains("rtl_dir")) {
		out_cfg.sv_path = fpga.value("rtl_dir", "");
	}

	if (out_cfg.sv_path.empty()) {
		out_cfg.sv_path = detect_sv_path(project_root);
	}

	out_cfg.yosys_synth_args = fpga.value("yosys_synth_args", "");
	return true;
}

bool fpga_project_settings::save_to_project(std::string_view project_root, const fpga_target_config &cfg, std::string &out_path_used, std::string &out_error)
{
	fs::path root(project_root.empty() ? "." : project_root);
	fs::path target_path;

	if (!cfg.active_config_file.empty()) {
		target_path = cfg.active_config_file;
	} else {
		fs::path project_json = root / "project.json";
		std::error_code ec;
		if (fs::exists(project_json, ec)) {
			target_path = project_json;
		} else {
			target_path = root / "turbostar.json";
		}
	}

	nlohmann::json j = nlohmann::json::object();
	std::error_code ec;
	if (fs::exists(target_path, ec)) {
		std::ifstream in(target_path);
		if (in.is_open()) {
			try {
				in >> j;
			} catch (...) {
				j = nlohmann::json::object();
			}
		}
	}

	nlohmann::json fpga_obj = j.value("fpga", nlohmann::json::object());
	if (!cfg.target.empty()) fpga_obj["target"] = cfg.target;
	if (!cfg.device.empty()) fpga_obj["device"] = cfg.device;
	if (!cfg.package.empty()) fpga_obj["package"] = cfg.package;
	if (!cfg.top_module.empty()) fpga_obj["top_module"] = cfg.top_module;
	if (cfg.freq_mhz > 0.0) fpga_obj["freq_mhz"] = cfg.freq_mhz;
	if (!cfg.pcf_file.empty()) fpga_obj["pcf_file"] = cfg.pcf_file;
	if (!cfg.sv_path.empty()) fpga_obj["sv_path"] = cfg.sv_path;
	if (!cfg.yosys_synth_args.empty()) fpga_obj["yosys_synth_args"] = cfg.yosys_synth_args;

	j["fpga"] = fpga_obj;

	std::ofstream out(target_path);
	if (!out.is_open()) {
		out_error = std::format("Failed to open '{}' for writing", target_path.string());
		return false;
	}

	out << j.dump(2) << "\n";
	out_path_used = target_path.string();
	return true;
}

bool fpga_project_settings::save_to_project(std::string_view project_root, fpga_target_config &cfg, std::string &out_path_used, std::string &out_error)
{
	bool ok = save_to_project(project_root, static_cast<const fpga_target_config &>(cfg), out_path_used, out_error);
	if (ok) {
		cfg.active_config_file = out_path_used;
	}
	return ok;
}

bool fpga_project_settings::is_complete(const fpga_target_config &cfg, std::vector<std::string> &out_missing_fields)
{
	out_missing_fields.clear();
	if (cfg.target.empty()) out_missing_fields.push_back("target");
	if (cfg.device.empty()) out_missing_fields.push_back("device");
	if (cfg.top_module.empty()) out_missing_fields.push_back("top_module");
	if (cfg.freq_mhz <= 0.0) out_missing_fields.push_back("freq_mhz");
	return out_missing_fields.empty();
}

std::string fpga_project_settings::format_summary(const fpga_target_config &cfg, const fpga_system_tools &tools,
                                                  std::span<const std::string> pcf_files)
{
	std::string md = "## FPGA Project Settings\n\n";
	md += std::format("- **Config File**: {}\n", cfg.active_config_file.empty() ? "*None (not configured)*" : std::format("`{}`", cfg.active_config_file));
	md += std::format("- **Target Architecture**: {}\n", cfg.target.empty() ? "*Unset*" : std::format("`{}`", cfg.target));
	md += std::format("- **Device**: {}\n", cfg.device.empty() ? "*Unset*" : std::format("`{}`", cfg.device));
	md += std::format("- **Package**: {}\n", cfg.package.empty() ? "*None / Unset*" : std::format("`{}`", cfg.package));
	md += std::format("- **Top Module**: {}\n", cfg.top_module.empty() ? "*Unset*" : std::format("`{}`", cfg.top_module));
	md += std::format("- **Clock Frequency**: {}\n", cfg.freq_mhz > 0.0 ? std::format("{:.1f} MHz", cfg.freq_mhz) : "*Unset*");
	md += std::format("- **PCF Pin Constraints**: {}\n", cfg.pcf_file.empty() ? "*None*" : std::format("`{}`", cfg.pcf_file));
	md += std::format("- **RTL Source Directory**: {}\n", cfg.sv_path.empty() ? "*Auto-detect (*.sv)*" : std::format("`{}`", cfg.sv_path));
	if (!cfg.yosys_synth_args.empty()) {
		md += std::format("- **Synthesis Args**: `{}`\n", cfg.yosys_synth_args);
	}

	md += "\n### System EDA Environment\n";
	md += std::format("- **Yosys**: {}\n", tools.yosys_path.empty() ? "*Not found*" : std::format("`{}`", tools.yosys_path));
	if (!tools.available_targets.empty()) {
		std::string targets_str;
		for (size_t i = 0; i < tools.available_targets.size(); ++i) {
			if (i > 0) targets_str += ", ";
			targets_str += tools.available_targets[i];
		}
		md += std::format("- **Installed nextpnr Targets**: `{}`\n", targets_str);
	} else {
		md += "- **Installed nextpnr Targets**: *None found on PATH*\n";
	}

	if (!pcf_files.empty()) {
		md += "\n### Discovered Pin Constraints (.pcf)\n";
		for (const auto &pcf : pcf_files) {
			md += std::format("- `{}`\n", pcf);
		}
	}

	return md;
}

std::string fpga_project_settings::format_guidance(std::string_view /*project_root*/, const fpga_target_config &cfg,
                                                   const fpga_system_tools &tools, std::span<const std::string> pcf_files)
{
	std::vector<std::string> missing;
	is_complete(cfg, missing);

	std::string md = "## Missing FPGA Project Configuration\n\n";
	if (is_cli_mode()) {
		md += "To run physical timing analysis (`turbostar-verilog timing --pnr`), the design requires hardware target parameters.\n\n";
	} else {
		md += "To run physical timing analysis (`verilog_pnr_timing`), the design requires hardware target parameters.\n\n";
	}

	md += "### System & Project Context\n";
	if (tools.available_targets.size() == 1) {
		md += std::format("- **EDA Binary**: Found installed target `{}` at `{}`\n",
		                  tools.available_targets.front(),
		                  (tools.available_targets.front() == "ice40") ? tools.nextpnr_ice40_path :
		                  (tools.available_targets.front() == "generic") ? tools.nextpnr_generic_path : tools.nextpnr_ecp5_path);
	} else if (!tools.available_targets.empty()) {
		std::string s;
		for (size_t i = 0; i < tools.available_targets.size(); ++i) {
			if (i > 0) s += ", ";
			s += tools.available_targets[i];
		}
		md += std::format("- **EDA Binaries**: Available nextpnr targets on PATH: `{}`\n", s);
	} else {
		md += "- **EDA Binaries**: No `nextpnr` executable was found on PATH.\n";
	}

	if (pcf_files.size() == 1) {
		md += std::format("- **Pin Constraints**: Found single constraint file `{}` (auto-usable)\n", pcf_files.front());
	} else if (pcf_files.size() > 1) {
		std::string pcfs;
		for (size_t i = 0; i < pcf_files.size(); ++i) {
			if (i > 0) pcfs += ", ";
			pcfs += pcf_files[i];
		}
		md += std::format("- **Pin Constraints**: Multiple .pcf files found: `{}`\n", pcfs);
	}

	md += "\n### Required Open Settings\n\n";

	for (const auto &field : missing) {
		if (field == "target") {
			md += "#### 1. Target Architecture (`target`)\n";
			md += "Specifies the FPGA family (e.g. `\"ice40\"`, `\"generic\"`, `\"ecp5\"`).\n";
			md += "- *How to resolve*: Inspect the project `Makefile`, `meson.build`, or board documentation for the synthesis pass (e.g. `synth_ice40`) or nextpnr tool (`nextpnr-ice40`).";
			if (tools.available_targets.size() == 1) {
				md += std::format(" Recommended default: `{}`.", tools.available_targets.front());
			}
			md += "\n\n";
		} else if (field == "device") {
			md += "#### 2. Device & Package (`device`, `package`)\n";
			md += "Specifies the target FPGA chip and package (e.g. `device: \"hx8k\"`, `package: \"cb132\"` or `\"--hx8k --package cb132\"`).\n";
			md += "- *How to resolve*: Look for nextpnr command flags in the `Makefile` or build scripts, or check the board documentation.\n\n";
		} else if (field == "top_module") {
			md += "#### 3. Top-Level Module (`top_module`)\n";
			md += "Specifies the root synthesizable SystemVerilog module name (e.g. `\"main\"` or `\"top\"`).\n";
			md += "- *How to resolve*: Check the top module specified in the synthesis invocation in `Makefile` (e.g. `-top main`) or the `(* top *)` attribute in the RTL files.\n\n";
		} else if (field == "freq_mhz") {
			md += "#### 4. Target Clock Frequency (`freq_mhz`)\n";
			md += "Specifies the design clock frequency in MHz (e.g. `60.0` or `100.0`).\n";
			md += "- *How to resolve*: Look for `--freq <MHz>` in nextpnr commands in `Makefile`, or check the oscillator frequency in the board `.pcf` or clock generation module.\n\n";
		}
	}

	md += "### Remediation Action\n";
	std::string rec_target = (!cfg.target.empty()) ? cfg.target : ((tools.available_targets.size() == 1) ? tools.available_targets.front() : "ice40");
	std::string rec_device = cfg.device.empty() ? "hx8k" : cfg.device;
	std::string rec_top = cfg.top_module.empty() ? "main" : cfg.top_module;
	double rec_freq = cfg.freq_mhz > 0.0 ? cfg.freq_mhz : 60.0;
	if (is_cli_mode()) {
		md += "Run `turbostar-verilog config` to configure and save:\n\n";
		md += "```sh\n";
		std::string cmd = std::format("turbostar-verilog config --target {} --device {}", rec_target, rec_device);
		if (!cfg.package.empty()) {
			cmd += std::format(" --package {}", cfg.package);
		}
		cmd += std::format(" --top {} --freq {}", rec_top, rec_freq);
		if (!cfg.sv_path.empty()) {
			cmd += std::format(" --sv-path {}", cfg.sv_path);
		}
		if (!cfg.pcf_file.empty()) {
			cmd += std::format(" --pcf {}", cfg.pcf_file);
		} else if (pcf_files.size() == 1) {
			cmd += std::format(" --pcf {}", pcf_files.front());
		}
		md += cmd + "\n```\n";
	} else {
		md += "Inspect the project files or query the user, then call `verilog_project_settings` to configure and save:\n\n";
		md += "```json\n";
		md += "{\n";
		md += std::format("  \"target\": \"{}\",\n", rec_target);
		md += std::format("  \"device\": \"{}\",\n", rec_device);
		if (!cfg.package.empty()) {
			md += std::format("  \"package\": \"{}\",\n", cfg.package);
		}
		md += std::format("  \"top_module\": \"{}\",\n", rec_top);
		md += std::format("  \"freq_mhz\": {},\n", rec_freq);
		if (!cfg.sv_path.empty()) {
			md += std::format("  \"sv_path\": \"{}\",\n", cfg.sv_path);
		}
		if (!cfg.pcf_file.empty()) {
			md += std::format("  \"pcf_file\": \"{}\"\n", cfg.pcf_file);
		} else if (pcf_files.size() == 1) {
			md += std::format("  \"pcf_file\": \"{}\"\n", pcf_files.front());
		} else {
			md += "  \"pcf_file\": \"\"\n";
		}
		md += "}\n";
		md += "```\n";
	}

	return md;
}

std::string fpga_project_settings::normalize_test_level(std::string_view level)
{
	std::string lower;
	lower.reserve(level.size());
	for (unsigned char c : level) {
		if (!std::isspace(c)) {
			lower.push_back(static_cast<char>(std::tolower(c)));
		}
	}
	if (lower.empty() || lower == "quick" || lower == "fast" || lower == "smoke" || lower == "unit") {
		return "quick";
	}
	if (lower == "comprehensive" || lower == "normal" || lower == "standard" || lower == "default" || lower == "medium" || lower == "thorough") {
		return "comprehensive";
	}
	if (lower == "extreme" || lower == "exhaustive" || lower == "long" || lower == "full" || lower == "all" || lower == "stress") {
		return "extreme";
	}
	return "";
}

int fpga_project_settings::test_level_rank(std::string_view level) noexcept
{
	std::string norm = normalize_test_level(level);
	if (norm == "quick") {
		return 1;
	}
	if (norm == "comprehensive") {
		return 2;
	}
	if (norm == "extreme") {
		return 3;
	}
	return 2;
}

bool fpga_project_settings::load_testbenches(std::string_view project_root, std::vector<testbench_entry> &out_entries,
                                             std::string &out_config_path, std::string &out_error)
{
	out_entries.clear();
	out_config_path.clear();
	out_error.clear();

	fs::path root(project_root.empty() ? "." : project_root);
	std::vector<std::string> candidates = {"turbostar.json", "project.json"};

	fs::path found_path;
	for (const auto &cand : candidates) {
		fs::path p = root / cand;
		std::error_code ec;
		if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
			found_path = p;
			break;
		}
	}

	if (found_path.empty()) {
		return true;
	}

	out_config_path = found_path.string();
	std::ifstream f(found_path);
	if (!f.is_open()) {
		out_error = std::format("Failed to open config file '{}'", found_path.string());
		return false;
	}

	nlohmann::json untrusted_j;
	try {
		f >> untrusted_j;
	} catch (const std::exception &e) {
		out_error = std::format("JSON parse error in '{}': {}", found_path.string(), e.what());
		return false;
	}

	if (!untrusted_j.contains("testbenches")) {
		return true;
	}

	auto parse_entry_obj = [&](const std::string &untrusted_tb_key, const nlohmann::json &untrusted_item) {
		std::string untrusted_tb = untrusted_tb_key;
		if (untrusted_tb.empty() && untrusted_item.contains("testbench") && untrusted_item["testbench"].is_string()) {
			untrusted_tb = untrusted_item["testbench"].get<std::string>();
		}
		if (untrusted_tb.empty() || !fs_utils::is_safe_for_ui(untrusted_tb)) {
			return;
		}
		testbench_entry entry;
		entry.testbench = untrusted_tb;

		std::string untrusted_lvl = "quick";
		if (untrusted_item.contains("level") && untrusted_item["level"].is_string()) {
			untrusted_lvl = untrusted_item["level"].get<std::string>();
		}
		std::string clean_lvl = normalize_test_level(untrusted_lvl);
		entry.level = clean_lvl.empty() ? "quick" : clean_lvl;

		auto add_mod = [&](const std::string &untrusted_m) {
			if (!untrusted_m.empty() && fs_utils::is_safe_for_ui(untrusted_m)) {
				if (std::find(entry.modules.begin(), entry.modules.end(), untrusted_m) == entry.modules.end()) {
					entry.modules.push_back(untrusted_m);
				}
			}
		};

		if (untrusted_item.contains("modules") && untrusted_item["modules"].is_array()) {
			for (const auto &untrusted_m : untrusted_item["modules"]) {
				if (untrusted_m.is_string()) {
					add_mod(untrusted_m.get<std::string>());
				}
			}
		} else if (untrusted_item.contains("modules") && untrusted_item["modules"].is_string()) {
			add_mod(untrusted_item["modules"].get<std::string>());
		}
		if (untrusted_item.contains("module") && untrusted_item["module"].is_string()) {
			add_mod(untrusted_item["module"].get<std::string>());
		}

		out_entries.push_back(std::move(entry));
	};

	const auto &untrusted_tbs = untrusted_j["testbenches"];
	if (untrusted_tbs.is_array()) {
		for (const auto &untrusted_item : untrusted_tbs) {
			if (untrusted_item.is_object()) {
				parse_entry_obj("", untrusted_item);
			}
		}
	} else if (untrusted_tbs.is_object()) {
		for (auto it = untrusted_tbs.begin(); it != untrusted_tbs.end(); ++it) {
			if (it.value().is_object()) {
				parse_entry_obj(it.key(), it.value());
			}
		}
	}

	return true;
}

bool fpga_project_settings::save_testbenches(std::string_view project_root, std::span<const testbench_entry> entries,
                                             std::string &out_path_used, std::string &out_error)
{
	fs::path root(project_root.empty() ? "." : project_root);
	fs::path target_path = root / "turbostar.json";
	std::error_code ec;
	if (!fs::exists(target_path, ec) && fs::exists(root / "project.json", ec)) {
		target_path = root / "project.json";
	}

	nlohmann::json j = nlohmann::json::object();
	if (fs::exists(target_path, ec)) {
		std::ifstream in(target_path);
		if (in.is_open()) {
			try {
				in >> j;
			} catch (...) {
				j = nlohmann::json::object();
			}
		}
	}
	if (!j.is_object()) {
		j = nlohmann::json::object();
	}

	nlohmann::json tb_arr = nlohmann::json::array();
	for (const auto &e : entries) {
		nlohmann::json item = nlohmann::json::object();
		item["testbench"] = e.testbench;
		item["modules"] = e.modules;
		item["level"] = e.level.empty() ? "quick" : e.level;
		tb_arr.push_back(std::move(item));
	}
	j["testbenches"] = std::move(tb_arr);

	std::ofstream out(target_path);
	if (!out.is_open()) {
		out_error = std::format("Failed to open '{}' for writing", target_path.string());
		return false;
	}

	out << j.dump(2) << "\n";
	out_path_used = target_path.string();
	return true;
}

bool fpga_project_settings::load_hierarchical_modules(std::string_view project_root, std::vector<std::string> &out_modules,
                                                      std::string &out_error)
{
	out_modules.clear();
	out_error.clear();

	fs::path root(project_root.empty() ? "." : project_root);
	std::vector<std::string> candidates = {"turbostar.json", "project.json"};

	fs::path found_path;
	for (const auto &cand : candidates) {
		fs::path p = root / cand;
		std::error_code ec;
		if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
			found_path = p;
			break;
		}
	}

	if (found_path.empty()) {
		return true;
	}

	std::ifstream f(found_path);
	if (!f.is_open()) {
		out_error = std::format("Failed to open config file '{}'", found_path.string());
		return false;
	}

	nlohmann::json untrusted_j;
	try {
		f >> untrusted_j;
	} catch (const std::exception &e) {
		out_error = std::format("JSON parse error in '{}': {}", found_path.string(), e.what());
		return false;
	}

	if (!untrusted_j.contains("hierarchical_modules") || !untrusted_j["hierarchical_modules"].is_array()) {
		return true;
	}

	for (const auto &untrusted_m : untrusted_j["hierarchical_modules"]) {
		if (untrusted_m.is_string()) {
			std::string untrusted_str = untrusted_m.get<std::string>();
			if (!untrusted_str.empty() && fs_utils::is_safe_for_ui(untrusted_str)) {
				if (std::find(out_modules.begin(), out_modules.end(), untrusted_str) == out_modules.end()) {
					out_modules.push_back(untrusted_str);
				}
			}
		}
	}

	return true;
}

bool fpga_project_settings::save_hierarchical_modules(std::string_view project_root, std::span<const std::string> modules,
                                                      std::string &out_path_used, std::string &out_error)
{
	fs::path root(project_root.empty() ? "." : project_root);
	fs::path target_path = root / "turbostar.json";
	std::error_code ec;
	if (!fs::exists(target_path, ec) && fs::exists(root / "project.json", ec)) {
		target_path = root / "project.json";
	}

	nlohmann::json j = nlohmann::json::object();
	if (fs::exists(target_path, ec)) {
		std::ifstream in(target_path);
		if (in.is_open()) {
			try {
				in >> j;
			} catch (...) {
				j = nlohmann::json::object();
			}
		}
	}
	if (!j.is_object()) {
		j = nlohmann::json::object();
	}

	nlohmann::json mod_arr = nlohmann::json::array();
	for (const auto &m : modules) {
		mod_arr.push_back(m);
	}
	j["hierarchical_modules"] = std::move(mod_arr);

	std::ofstream out(target_path);
	if (!out.is_open()) {
		out_error = std::format("Failed to open '{}' for writing", target_path.string());
		return false;
	}

	out << j.dump(2) << "\n";
	out_path_used = target_path.string();
	return true;
}

} // namespace verilog
