#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace verilog
{

class vcd_parser;

/**
 * @brief Information about a discovered Verilog / SystemVerilog module declaration.
 */
struct discovered_module {
	std::string module_name;
	std::string source_file;
};

/**
 * @brief Information about a declared input/output/inout port of a Verilog/SystemVerilog module.
 */
struct module_port_info {
	std::string name;
	std::string direction; // "input", "output", or "inout"
};

/**
 * @brief Resolved VCD scope, RTL module type, and declared ports for a requested instance or module name.
 */
struct resolved_vcd_instance {
	std::string scope;                       // e.g. "tb_main.uut.cpu8008.alu"
	std::string module_name;                 // e.g. "alu_mod"
	std::vector<module_port_info> ports;     // Declared input/inout/output ports in source order
	std::vector<std::string> other_scopes;   // Additional matching instance scopes in the VCD (if multiple exist)
};

/**
 * @brief Scanner utility for discovering synthesizable Verilog files and module declarations.
 */
class verilog_module_scanner
{
public:
	/**
	 * @brief Returns true if a path is ignored (e.g. backup files, hidden files, VCS/build directories, or testbenches).
	 */
	static bool is_ignored_file(const std::filesystem::path &p);

	/**
	 * @brief Strips line and block comments from source code while preserving string literals.
	 */
	static std::string strip_comments(std::string_view code);

	/**
	 * @brief Finds all synthesizable .sv / .v source files under the given root file or directory.
	 */
	static std::vector<std::string> find_synthesizable_files(const std::filesystem::path &root);

	/**
	 * @brief Parses source files and extracts declared module names and source file paths.
	 */
	static std::vector<discovered_module> discover_modules_in_files(std::span<const std::string> files);
	/**
	 * @brief Convenience method to find files and discover modules in one step.
	 */
	static std::vector<discovered_module> scan(const std::filesystem::path &root, std::vector<std::string> &out_all_files);

	/**
	 * @brief Returns true if a path looks like a Verilog/SystemVerilog testbench file.
	 */
	static bool is_testbench_file(const std::filesystem::path &p);

	/**
	 * @brief Finds all testbench .sv / .v files under the given root file or directory.
	 */
	static std::vector<std::string> find_testbench_files(const std::filesystem::path &root);

	/**
	 * @brief Scans source files for $readmemh / $readmemb literals and symlinks or stages missing data files into work_dir.
	 */
	static std::vector<std::string> stage_readmem_files(std::span<const std::string> source_files, const std::filesystem::path &work_dir);

	/**
	 * @brief Extracts the VCD filename from $dumpfile("...") in a testbench file, returns "dump.vcd" if
	 *        $dumpvars/$dumpfile() is used without an explicit filename, or returns "" if the testbench
	 *        does not dump waveforms.
	 */
	static std::string extract_dumpfile_path(const std::filesystem::path &testbench_file);

	/**
	 * @brief Resolves a requested (or empty) VCD path against the project's testbench $dumpfile("...") declaration.
	 *        When requested_path is empty and requested_instance is non-empty, prefers an existing VCD file that
	 *        contains the requested module instance or module type.
	 */
	static std::string resolve_vcd_path(const std::filesystem::path &project_root,
	                                    std::string_view requested_path,
	                                    std::string_view requested_instance = "");

	/**
	 * @brief Extracts the declared input/output/inout ports of module_name from all_rtl_sources in declaration order.
	 */
	static std::vector<module_port_info> extract_module_ports(std::string_view module_name,
	                                                          std::span<const std::string> all_rtl_sources);

	/**
	 * @brief Resolves a requested module instance name/path (e.g. "alu", "cpu8008.alu") or RTL module name
	 *        (e.g. "alu_mod", "register") against the scopes present in a parsed VCD and the project's RTL sources.
	 *        If multiple instances of a module exist in the VCD, selects the first scope and populates other_scopes.
	 */
	static std::optional<resolved_vcd_instance> resolve_vcd_instance_scope(const vcd_parser &parser,
	                                                                       std::string_view requested_instance,
	                                                                       const std::filesystem::path &project_root);

	/**
	 * @brief Resolves a requested module name (or empty string when safe_file is provided) against
	 *        the declared modules discovered in safe_file and all_sources (matching module names,
	 *        source file stems, and RTL submodule instance names).
	 */
	static std::string resolve_module_name(std::string_view requested_module,
	                                       std::string_view safe_file,
	                                       std::span<const std::string> all_sources);

	/**
	 * @brief Scans SystemVerilog (.sv / .v) files under root for typedef enum and inline enum
	 *        declarations using mapped_file and ::memmem fast-paths, resolves signals declared
	 *        with those enum types inside modules and their instantiated hierarchy names, and
	 *        returns a map of signal patterns (e.g. "*sequencer.state") to hex value -> enum label maps.
	 */
	static std::map<std::string, std::map<std::string, std::string>> discover_enum_signal_labels(const std::filesystem::path &root);

	/**
	 * @brief Scans a Verilog/SystemVerilog source or testbench file and returns the deduplicated list
	 *        of known RTL module names (from all_rtl_sources) directly instantiated inside it.
	 */
	static std::vector<std::string> discover_instantiated_modules(const std::filesystem::path &source_file,
	                                                              std::span<const std::string> all_rtl_sources);

	/**
	 * @brief Resolves the transitive submodule hierarchy rooted at root_module across all_rtl_sources
	 *        in bottom-up (post-order) sequence: leaf submodules first, root_module last, deduplicated.
	 */
	static std::vector<std::string> discover_submodule_hierarchy(std::string_view root_module,
	                                                             std::span<const std::string> all_rtl_sources);
};

} // namespace verilog
