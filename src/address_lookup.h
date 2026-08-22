#pragma once

#include <span>
#include <string>
#include <string_view>
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
	int column_number{0};
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
	static std::vector<memory_mapping> parse_maps(std::string_view maps_path_or_pid);

	// Resolve a single memory address to function name, file path, and line number.
	static resolved_address resolve_address(uintptr_t address, std::string_view maps_path_or_pid = "");

	// Resolve a batch of memory addresses in high-performance bulk mode.
	// Uses address deduplication, batch execution, and persistent eu-addr2line/addr2line invocations.
	static std::vector<resolved_address> resolve_addresses(std::span<const uintptr_t> addresses,
							       std::string_view maps_path_or_pid = "");

	// Safely execute subprocess without shell interpretation, passing stdin content if provided
	static std::vector<std::string> run_command(std::string_view bin, std::span<const std::string> args,
						    std::string_view stdin_input = "");

      private:
	// Check if a binary exists in PATH
	static bool check_binary_exists(std::string_view name);
};


} // namespace turbostar
