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

std::string vcd_parser_cache::check_stale_rtl_warning(std::string_view safe_vcd_path, std::string_view display_vcd_path)
{
	std::error_code ec;
	auto vcd_mtime = std::filesystem::last_write_time(safe_vcd_path, ec);
	if (ec) {
		return "";
	}

	std::filesystem::path vcd_p(safe_vcd_path);
	std::filesystem::path parent_dir = vcd_p.parent_path();
	if (parent_dir.empty()) {
		parent_dir = ".";
	}

	std::vector<std::filesystem::path> scan_dirs = {parent_dir};
	if (std::filesystem::exists(parent_dir / "sv", ec) && std::filesystem::is_directory(parent_dir / "sv", ec)) {
		scan_dirs.push_back(parent_dir / "sv");
	}
	if (parent_dir.has_parent_path()) {
		auto sib_sv = parent_dir.parent_path() / "sv";
		if (std::filesystem::exists(sib_sv, ec) && std::filesystem::is_directory(sib_sv, ec)) {
			scan_dirs.push_back(sib_sv);
		}
	}

	std::filesystem::file_time_type newest_mtime = vcd_mtime;
	std::string newest_rtl;

	for (const auto &dir : scan_dirs) {
		std::filesystem::recursive_directory_iterator it(
			dir, std::filesystem::directory_options::skip_permission_denied, ec);
		std::filesystem::recursive_directory_iterator end;
		while (!ec && it != end) {
			if (it.depth() > 2) {
				it.disable_recursion_pending();
			} else if (it->is_directory(ec)) {
				std::string dname = it->path().filename().string();
				if (dname.starts_with(".") || dname == "build" || dname == "subprojects") {
					it.disable_recursion_pending();
				}
			} else if (it->is_regular_file(ec)) {
				auto ext = it->path().extension().string();
				if (ext == ".sv" || ext == ".v") {
					auto f_mtime = it->last_write_time(ec);
					if (!ec && f_mtime > newest_mtime) {
						newest_mtime = f_mtime;
						newest_rtl = it->path().filename().string();
					}
				}
			}
			it.increment(ec);
		}
	}

	if (!newest_rtl.empty()) {
		std::string shown_vcd = display_vcd_path.empty() ? vcd_p.filename().string() : std::string(display_vcd_path);
		return std::format("> [!WARNING] Stale VCD: RTL source file `{}` was modified more recently than `{}`. Re-run simulation to update the waveform.\n\n",
		                   newest_rtl, shown_vcd);
	}
	return "";
}

} // namespace verilog
