#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace verilog
{

class vcd_parser;

struct vcd_cache_entry {
	std::string path;
	std::filesystem::file_time_type mtime;
	uintmax_t file_size{0};
	std::shared_ptr<const vcd_parser> parser;
};

class vcd_parser_cache
{
      public:
	static constexpr size_t max_entries = 2;

	static vcd_parser_cache &get_instance();

	// Retrieves an existing parsed VCD parser from cache if path, mtime, and size match.
	// Otherwise, parses the file, updates the 2-slot LRU cache, and returns the new parser.
	// Returns nullptr and sets out_error on parse failure.
	std::shared_ptr<const vcd_parser> get_or_parse(std::string_view safe_path, std::string &out_error);

	// Clears all cached entries (useful for test isolation or memory reclamation)
	void clear();

	// Returns the current number of cached entries
	size_t size() const;

	// Checks if any RTL source file (.sv / .v) in the VCD's enclosing directory (or sibling sv/ directory)
	// has a newer modification time than the VCD file itself. Returns a Markdown warning banner if stale.
	static std::string check_stale_rtl_warning(std::string_view safe_vcd_path, std::string_view display_vcd_path = "");

      private:
	vcd_parser_cache() = default;
	~vcd_parser_cache() = default;
	vcd_parser_cache(const vcd_parser_cache &) = delete;
	vcd_parser_cache &operator=(const vcd_parser_cache &) = delete;
	// mutex_ protects entries_:
	// (1) Protects the entries_ vector against concurrent reads, inserts, and LRU reorderings.
	// (2) Locking rules: Locks are acquired briefly to lookup, promote, or insert cache entries.
	//     File parsing (I/O) is performed outside this lock to avoid holding the lock across long-running operations.
	mutable std::mutex mutex_;
	std::vector<vcd_cache_entry> entries_;
};

} // namespace verilog
