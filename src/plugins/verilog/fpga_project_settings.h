#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <optional>

namespace verilog {

/**
 * @brief Configuration data for FPGA synthesis and place-and-route targeting.
 */
struct fpga_target_config {
	std::string target;           // FPGA architecture/target, e.g. "ice40", "generic", "ecp5"
	std::string device;           // Device type or flag, e.g. "hx8k" or "--hx8k"
	std::string package;          // Package type or flag, e.g. "cb132" or "--package cb132"
	std::string top_module;       // Synthesizable top-level module name, e.g. "main"
	double freq_mhz{0.0};         // Target clock frequency in MHz, e.g. 60.0
	std::string pcf_file;         // Physical constraints file path, e.g. "pcf/cu.pcf"
	std::string sv_path;          // Root directory for synthesizable RTL, e.g. "sv" or "src"
	std::string yosys_synth_args; // Additional synthesis args, e.g. "-abc9"
	std::string active_config_file; // Source file path loaded from or written to
};

/**
 * @brief Association between a testbench file, one or more RTL modules, and a test level tier.
 */
struct testbench_entry {
	std::string testbench;            // Project-relative path to the testbench file, e.g. "tb/tb_alu.sv"
	std::vector<std::string> modules; // Canonical RTL module names tested by this testbench, e.g. ["alu_mod"]
	std::string level{"quick"};       // Test tier: "quick", "comprehensive", or "extreme"
};

/**
 * @brief Discovered hardware EDA tools installed on the host system.
 */
struct fpga_system_tools {
	std::string yosys_path;
	std::string nextpnr_ice40_path;
	std::string nextpnr_generic_path;
	std::string nextpnr_ecp5_path;
	std::vector<std::string> available_targets; // e.g. ["ice40", "generic"]
};

/**
 * @brief Project FPGA settings manager.
 *
 * Reads and persists FPGA configuration in turbostar.json or project.json,
 * scans for pin constraint files (*.pcf), inspects installed EDA tools on PATH,
 * and generates actionable guidance for LLMs when parameters are missing.
 */
class fpga_project_settings {
public:
	// Detect EDA tool binaries installed on PATH
	static fpga_system_tools detect_system_tools();

	// Auto-detect synthesizable RTL directory (checks "sv", "rtl", "src", or ".")
	static std::string detect_sv_path(std::string_view project_root);

	// Scan project directory for *.pcf constraint files
	static std::vector<std::string> scan_pcf_files(std::string_view project_root);

	// Load configuration from project_root (checks turbostar.json then project.json)
	static bool load_from_project(std::string_view project_root, fpga_target_config &out_cfg, std::string &out_error);

	// Save or update configuration in project_root with 2-space formatted JSON
	static bool save_to_project(std::string_view project_root, const fpga_target_config &cfg, std::string &out_path_used, std::string &out_error);
	static bool save_to_project(std::string_view project_root, fpga_target_config &cfg, std::string &out_path_used, std::string &out_error);
	// Check if all mandatory fields for PNR are present
	static bool is_complete(const fpga_target_config &cfg, std::vector<std::string> &out_missing_fields);

	// Produce actionable guidance explaining missing parameters and how to resolve them
	static std::string format_guidance(std::string_view project_root, const fpga_target_config &cfg,
	                                   const fpga_system_tools &tools, std::span<const std::string> pcf_files);

	// Format a Markdown summary of current project FPGA settings and detected environment
	static std::string format_summary(const fpga_target_config &cfg, const fpga_system_tools &tools,
	                                  std::span<const std::string> pcf_files);

	// Normalize a test level string ("quick", "comprehensive", "extreme"); returns empty string if unrecognized
	static std::string normalize_test_level(std::string_view level);

	// Return cumulative numeric rank for a test level ("quick" -> 1, "comprehensive" -> 2, "extreme" -> 3)
	static int test_level_rank(std::string_view level) noexcept;

	// Load configured testbench associations from turbostar.json / project.json
	static bool load_testbenches(std::string_view project_root, std::vector<testbench_entry> &out_entries,
	                             std::string &out_config_path, std::string &out_error);

	// Save configured testbench associations into turbostar.json / project.json preserving existing keys
	static bool save_testbenches(std::string_view project_root, std::span<const testbench_entry> entries,
	                             std::string &out_path_used, std::string &out_error);

	// Load modules configured with default hierarchical=true from turbostar.json / project.json
	static bool load_hierarchical_modules(std::string_view project_root, std::vector<std::string> &out_modules,
	                                      std::string &out_error);

	// Save modules configured with default hierarchical=true into turbostar.json / project.json
	static bool save_hierarchical_modules(std::string_view project_root, std::span<const std::string> modules,
	                                      std::string &out_path_used, std::string &out_error);
};

} // namespace verilog
