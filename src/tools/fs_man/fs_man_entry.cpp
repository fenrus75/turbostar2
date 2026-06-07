#include "fs_man.h"
#include "../troff2md.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <set>
#include <sstream>
#include <vector>
#include <zlib.h>

namespace tools {

struct man_candidate {
	std::filesystem::path path;
	std::string suffix;
	int priority;
	size_t suffix_len;
};

// Returns a priority score for a section suffix (lower is higher priority).
static int get_section_priority(const std::string& suffix) {
	if (suffix.empty()) return 999;
	char first = suffix[0];
	switch (first) {
		case '3': return 1;
		case '2': return 2;
		case '1': return 3;
		case '8': return 4;
		case '5': return 5;
		case '7': return 6;
		case '4': return 7;
		case '6': return 8;
		case '9': return 9;
		default: return 100;
	}
}

// Matches a man page file candidate against the requested name and section.
static bool match_man_file(const std::filesystem::path& file_path, const std::string& name, const std::optional<std::string>& section, std::string& out_section) {
	std::string filename = file_path.filename().string();
	
	// If it ends with .gz, strip it for matching.
	if (filename.ends_with(".gz")) {
		filename = filename.substr(0, filename.size() - 3);
	}
	
	// Filename must start with <name> + "."
	std::string prefix = name + ".";
	if (!filename.starts_with(prefix)) {
		return false;
	}
	
	// The rest is the section suffix.
	std::string suffix = filename.substr(prefix.size());
	if (suffix.empty()) {
		return false;
	}
	
	// If section is provided, suffix must start with it.
	if (section.has_value()) {
		if (!suffix.starts_with(*section)) {
			return false;
		}
	}
	
	out_section = suffix;
	return true;
}

// Decompresses a man page file. If the file ends in .gz, uses zlib.
static std::string decompress_man_page(const std::filesystem::path& path) {
	if (path.extension() == ".gz") {
		gzFile file = gzopen(path.c_str(), "rb");
		if (!file) return "";
		
		std::string content;
		char buffer[4096];
		int bytes_read;
		while ((bytes_read = gzread(file, buffer, sizeof(buffer))) > 0) {
			content.append(buffer, bytes_read);
		}
		gzclose(file);
		return content;
	} else {
		std::ifstream in(path, std::ios::binary);
		if (!in) return "";
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}
}

// Resolves a redirect path relative to base_dir
static std::filesystem::path find_redirect_file(const std::filesystem::path& base_dir, const std::string& rel_path) {
	std::filesystem::path target = base_dir / rel_path;
	if (std::filesystem::exists(target)) {
		return target;
	}
	
	// Check if .gz file exists on disk instead of uncompressed
	if (target.extension() != ".gz") {
		std::filesystem::path target_gz = target;
		target_gz += ".gz";
		if (std::filesystem::exists(target_gz)) {
			return target_gz;
		}
	} else {
		// Try without .gz extension
		std::string path_str = target.string();
		std::filesystem::path target_no_gz = path_str.substr(0, path_str.size() - 3);
		if (std::filesystem::exists(target_no_gz)) {
			return target_no_gz;
		}
	}
	return "";
}

fs_man_tool::fs_man_tool(fs_man_args args)
	: agentlib::llm_tool_action(std::format("Man page lookup: {}", args.name))
	, args_(std::move(args))
{}

bool fs_man_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
	// No runtime workspace resource validation is needed for global lookups.
	return true;
}

std::string fs_man_tool::execute(agentlib::tool_context& ctx) {
	const char* env_override = std::getenv("TURBOSTAR_MAN_DIR_OVERRIDE");
	std::filesystem::path base_dir = env_override ? std::filesystem::path(env_override) : std::filesystem::path("/usr/share/man");

	if (!std::filesystem::exists(base_dir)) {
		std::string err = std::format("Error: {} directory does not exist on this system.", base_dir.string());
		set_failure(ctx, err);
		return err;
	}

	std::vector<man_candidate> candidates;

	// Search all subdirectories directly under base_dir starting with "man"
	try {
		for (const auto& entry : std::filesystem::directory_iterator(base_dir)) {
			if (entry.is_directory()) {
				std::string dir_name = entry.path().filename().string();
				if (dir_name.starts_with("man")) {
					for (const auto& sub_entry : std::filesystem::directory_iterator(entry.path())) {
						if (sub_entry.is_regular_file()) {
							std::string suffix;
							if (match_man_file(sub_entry.path(), args_.name, args_.section, suffix)) {
								candidates.push_back({
									.path = sub_entry.path(),
									.suffix = suffix,
									.priority = get_section_priority(suffix),
									.suffix_len = suffix.length()
								});
							}
						}
					}
				}
			}
		}
	} catch (const std::exception& e) {
		std::string err = std::format("Error scanning man directory: {}", e.what());
		set_failure(ctx, err);
		return err;
	}

	if (candidates.empty()) {
		std::string err;
		if (args_.section.has_value()) {
			err = std::format("No man page found for '{}' in section '{}'.", args_.name, *args_.section);
		} else {
			err = std::format("No man page found for '{}'.", args_.name);
		}
		set_failure(ctx, err);
		return err;
	}

	// Sort candidates by priority (lower is higher priority), then by suffix length (prefer shorter suffix)
	std::sort(candidates.begin(), candidates.end(), [](const man_candidate& a, const man_candidate& b) {
		if (a.priority != b.priority) {
			return a.priority < b.priority;
		}
		return a.suffix_len < b.suffix_len;
	});

	std::filesystem::path current_path = candidates[0].path;
	std::set<std::filesystem::path> visited;
	int depth = 0;
	std::string raw_content;

	// Follow redirects up to a maximum depth of 5 with loop protection
	while (depth < 5) {
		visited.insert(current_path);
		raw_content = decompress_man_page(current_path);
		if (raw_content.empty()) {
			std::string err = std::format("Error: Failed to read man page at {}", current_path.string());
			set_failure(ctx, err);
			return err;
		}

		// Check if the page redirects to another page using .so
		if (raw_content.starts_with(".so ")) {
			std::string rel_path = raw_content.substr(4);
			// Trim spaces and quotes
			while (!rel_path.empty() && std::isspace(static_cast<unsigned char>(rel_path.front()))) {
				rel_path.erase(rel_path.begin());
			}
			while (!rel_path.empty() && std::isspace(static_cast<unsigned char>(rel_path.back()))) {
				rel_path.pop_back();
			}
			if (rel_path.size() >= 2 && rel_path.front() == '"' && rel_path.back() == '"') {
				rel_path = rel_path.substr(1, rel_path.size() - 2);
			}

			std::filesystem::path next_path = find_redirect_file(base_dir, rel_path);
			if (next_path.empty()) {
				std::string err = std::format("Error: Redirect target '{}' not found.", rel_path);
				set_failure(ctx, err);
				return err;
			}

			if (visited.contains(next_path)) {
				std::string err = std::format("Error: Circular redirect loop detected at {}", next_path.string());
				set_failure(ctx, err);
				return err;
			}

			current_path = next_path;
			depth++;
		} else {
			break;
		}
	}

	if (depth >= 5 && raw_content.starts_with(".so ")) {
		std::string err = "Error: Maximum redirect depth (5) exceeded.";
		set_failure(ctx, err);
		return err;
	}

	// Render troff format to Markdown
	std::string md_content = troff2md(raw_content);
	set_success(ctx, std::format("Successfully loaded man page for {}", args_.name));
	return md_content;
}

} // namespace tools
