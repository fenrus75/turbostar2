#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace verilog
{

/**
 * @brief Information about a discovered Verilog / SystemVerilog module declaration.
 */
struct discovered_module {
	std::string module_name;
	std::string source_file;
	std::string sidecar_path;
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
	 * @brief Parses source files and extracts declared module names and default sidecar paths.
	 */
	static std::vector<discovered_module> discover_modules_in_files(std::span<const std::string> files);

	/**
	 * @brief Convenience method to find files and discover modules in one step.
	 */
	static std::vector<discovered_module> scan(const std::filesystem::path &root, std::vector<std::string> &out_all_files);
};

} // namespace verilog
