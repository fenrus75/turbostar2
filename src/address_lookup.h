#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace turbostar
{

struct memory_mapping {
	uintptr_t start{0};
	uintptr_t end{0};
	uintptr_t offset{0};
	std::string pathname;
};

struct resolved_address {
	uintptr_t address{0};
	std::string function_name{"??"};
	std::string file_path{"??"};
	int line_number{0};
	std::string location{"??"};
};

/*
 * address_lookup handles high-performance translation of raw memory addresses
 * into resolved symbol names, source file paths, and line numbers.
 * Supports batching for high-frequency performance profiling and crash dump analysis.
 */
class address_lookup
{
      public:
	// Parse Linux /proc/<pid>/maps file or maps file path into memory_mapping entries.
	static std::vector<memory_mapping> parse_maps(const std::string &maps_path_or_pid);

	// Resolve a single memory address to function name, file path, and line number.
	static resolved_address resolve_address(uintptr_t address, const std::string &maps_path_or_pid = "");

	// Resolve a batch of memory addresses in high-performance bulk mode.
	// Uses address deduplication, batch execution, and persistent eu-addr2line/addr2line invocations.
	static std::vector<resolved_address> resolve_addresses(const std::vector<uintptr_t> &addresses,
							       const std::string &maps_path_or_pid = "");

      private:
	// Check if a binary exists in PATH
	static bool check_binary_exists(const std::string &name);

	// Safely execute subprocess without shell interpretation, passing stdin content if provided
	static std::vector<std::string> run_command(const std::string &bin, const std::vector<std::string> &args,
						    const std::string &stdin_input = "");
};

} // namespace turbostar
