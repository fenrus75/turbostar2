#include "plugins/verilog/vcd_parser_cache.h"
#include "plugins/verilog/vcd_parser.h"
#include <format>

namespace verilog
{

vcd_parser_cache &vcd_parser_cache::get_instance()
{
	static vcd_parser_cache instance;
	return instance;
}

std::shared_ptr<const vcd_parser> vcd_parser_cache::get_or_parse(std::string_view safe_path, std::string &out_error)
{
	std::error_code ec;
	auto mtime = std::filesystem::last_write_time(safe_path, ec);
	if (ec) {
		out_error = std::format("Failed to stat VCD file '{}': {}", safe_path, ec.message());
		return nullptr;
	}

	auto file_size = std::filesystem::file_size(safe_path, ec);
	if (ec) {
		out_error = std::format("Failed to get size of VCD file '{}': {}", safe_path, ec.message());
		return nullptr;
	}

	// 1. Fast cache lookup under lock
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (auto it = entries_.begin(); it != entries_.end(); ++it) {
			if (it->path == safe_path) {
				if (it->mtime == mtime && it->file_size == file_size) {
					// Cache hit! Promote to front (MRU)
					auto entry = *it;
					if (it != entries_.begin()) {
						entries_.erase(it);
						entries_.insert(entries_.begin(), entry);
					}
					return entry.parser;
				}
				// Stale entry found for this path - erase it
				entries_.erase(it);
				break;
			}
		}
	}

	// 2. Cache miss: Parse file outside lock to avoid blocking other threads
	auto new_parser = std::make_shared<vcd_parser>();
	if (!new_parser->parse_all(safe_path, out_error)) {
		return nullptr;
	}

	// 3. Insert newly parsed parser into cache under lock
	{
		std::lock_guard<std::mutex> lock(mutex_);
		// Re-check and remove any existing entry for this path
		for (auto it = entries_.begin(); it != entries_.end(); ++it) {
			if (it->path == safe_path) {
				entries_.erase(it);
				break;
			}
		}
		entries_.insert(entries_.begin(), {std::string(safe_path), mtime, file_size, new_parser});
		while (entries_.size() > max_entries) {
			entries_.pop_back();
		}
	}

	return new_parser;
}

void vcd_parser_cache::clear()
{
	std::lock_guard<std::mutex> lock(mutex_);
	entries_.clear();
}

size_t vcd_parser_cache::size() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return entries_.size();
}

} // namespace verilog
