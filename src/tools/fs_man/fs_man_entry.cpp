#include "fs_man.h"
#include "fs_utils.h"
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

// Decompresses a man page file up to 2MB. If the file ends in .gz, uses zlib.
static std::string decompress_man_page(const std::filesystem::path& path) {
	constexpr size_t max_bytes = 2 * 1024 * 1024; // 2MB cap
	if (path.extension() == ".gz") {
		gzFile file = gzopen(path.c_str(), "rb");
		if (!file) return "";
		
		std::string content;
		char buffer[4096];
		int bytes_read;
		while ((bytes_read = gzread(file, buffer, sizeof(buffer))) > 0) {
			content.append(buffer, bytes_read);
			if (content.size() >= max_bytes) {
				content.resize(max_bytes);
				break;
			}
		}
		gzclose(file);
		return content;
	} else {
		std::ifstream in(path, std::ios::binary);
		if (!in) return "";
		std::string content;
		char buffer[4096];
		while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
			content.append(buffer, in.gcount());
			if (content.size() >= max_bytes) {
				content.resize(max_bytes);
				break;
			}
		}
		return content;
	}
}

// Resolves a redirect path relative to base_dir, strictly validating containment
static std::filesystem::path find_redirect_file(const std::filesystem::path& base_dir, const std::string& rel_path) {
	if (rel_path.empty() || rel_path.front() == '/' || rel_path.front() == '\\' || rel_path.find("..") != std::string::npos) {
		return "";
	}

	std::filesystem::path target = (base_dir / rel_path).lexically_normal();
	std::filesystem::path base_norm = base_dir.lexically_normal();

	std::string target_str = target.string();
	std::string base_str = base_norm.string();
	if (!base_str.empty() && base_str.back() != '/') {
		base_str += '/';
	}
	if (!target_str.starts_with(base_str)) {
		return "";
	}

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

// Extracts a specific portion of a rendered man page Markdown document.
//
// The troff2md output has a consistent structure: top-level sections are rendered as
// "# Name" headings and individual directives are rendered as "*Name=*" entries. This
// function slices the document down to either:
//   - A section (matches a "# <name>" / "## <name>" heading, returning that
//     heading and everything up to the next same-or-higher-level heading), or
//   - A directive (matches a "*<name>*" or "*<name>=*" entry, returning the
//     block belonging to it).
// If no match is found, returns an empty string so the caller can fall back to a
// clear error.
static std::string filter_markdown(const std::string& md, const std::string& filter) {
	if (filter.empty()) {
		return md;
	}

	std::vector<std::string> lines;
	{
		std::istringstream ss(md);
		std::string line;
		while (std::getline(ss, line)) {
			lines.push_back(line);
		}
	}

	// First pass: try to match a section heading ("#" or "##" prefix). We look
	// for the first heading whose text contains the filter.
	int heading_level = -1; // 1 for "#", 2 for "##" (or deeper)
	int heading_index = -1;
	for (size_t i = 0; i < lines.size(); ++i) {
		const std::string& line = lines[i];
		if (line.size() >= 2 && line[0] == '#') {
			size_t j = 1;
			while (j < line.size() && line[j] == '#') {
				++j;
			}
			if (j == line.size() || line[j] != ' ') {
				continue; // Not a heading (e.g. "###" code fence or malformed)
			}
			std::string heading_text = line.substr(j + 1);
			// Normalize spaces for comparison
			std::string normalized;
			for (char c : heading_text) {
				if (c != ' ') {
					normalized += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
				}
			}
			std::string norm_filter;
			for (char c : filter) {
				if (c != ' ') {
					norm_filter += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
				}
			}
			if (!norm_filter.empty() && normalized.find(norm_filter) != std::string::npos) {
				heading_level = j;
				heading_index = static_cast<int>(i);
				break;
			}
		}
	}

	std::string result;
	if (heading_index >= 0) {
		result = lines[heading_index] + "\n";
		size_t block_level = static_cast<size_t>(heading_level);
		for (size_t i = heading_index + 1; i < lines.size(); ++i) {
			const std::string& line = lines[i];
			if (line.size() >= 2 && line[0] == '#') {
				size_t j = 1;
				while (j < line.size() && line[j] == '#') {
					++j;
				}
				if (j != line.size() && line[j] == ' ' && j <= block_level) {
					break; // Next heading at same or higher level ends this section
				}
			}
			result += line + "\n";
		}
		return result;
	}

	// Second pass: try to match a directive entry. A directive renders as a marker
	// consisting of an optional bullet "*" and/or bold "**" prefix followed by the
	// directive name and a trailing "=" (e.g. "*ProtectKernelTunables=*").
	auto is_directive_entry = [](const std::string& line) -> std::string {
		size_t start = line.find_first_not_of(" \t");
		if (start == std::string::npos || line[start] != '*') {
			return "";
		}
		size_t pos = start + 1;
		// Skip spaces and an optional bold "**" marker.
		while (pos < line.size() && line[pos] == ' ') {
			++pos;
		}
		if (pos + 1 < line.size() && line[pos] == '*' && line[pos + 1] == '*') {
			pos += 2;
		}
		// Read the directive token until a '=', '*' or whitespace terminator.
		std::string token;
		while (pos < line.size()) {
			char c = line[pos];
			if (c == '=' || c == '*' || c == ' ' || c == '\t') {
				break;
			}
			token += c;
			++pos;
		}
		// A true directive entry must be followed by '=' (distinguishes bullet lists).
		if (pos >= line.size() || line[pos] != '=') {
			return "";
		}
		return token;
	};

	std::string norm_filter;
	for (char c : filter) {
		if (c != ' ') {
			norm_filter += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		}
	}

	// Locate the first directive whose token matches the requested filter.
	int match_index = -1;
	for (size_t i = 0; i < lines.size(); ++i) {
		std::string token = is_directive_entry(lines[i]);
		if (token.empty()) {
			continue;
		}
		std::string norm_token;
		for (char c : token) {
			norm_token += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		}
		if (norm_token == norm_filter) {
			match_index = static_cast<int>(i);
			break;
		}
	}

	if (match_index >= 0) {
		result = lines[match_index] + "\n";
		// Capture from this directive until the next directive entry or any heading.
		for (size_t k = match_index + 1; k < lines.size(); ++k) {
			const std::string& next = lines[k];
			if (!next.empty() && next[0] == '#') {
				break;
			}
			if (!is_directive_entry(next).empty()) {
				break;
			}
			result += next + "\n";
		}
		return result;
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

	// Optionally slice the rendered Markdown down to the requested section/directive.
	if (!args_.filter.empty()) {
		std::string sliced = filter_markdown(md_content, args_.filter);
		if (sliced.empty()) {
			std::string err = std::format("Error: Filter '{}' did not match a section or directive in the man page for '{}'.", args_.filter, args_.name);
			set_failure(ctx, err);
			return err;
		}
		md_content = sliced;
	}

	if (!args_.safe_output_path.empty()) {
		bool is_vfs = (args_.safe_output_path.find("://") != std::string::npos);
		if (is_vfs) {
			auto vfs = ctx.fs_security.get_vfs();
			if (!vfs) {
				std::string err = "Error: VFS is not initialized in security context.";
				set_failure(ctx, err);
				return err;
			}
			std::string desc = vfs->write_file(args_.safe_output_path, md_content.data(), md_content.size());
			if (desc.empty()) {
				std::string err = std::format("Error: Failed to write output to VFS path '{}'.", args_.output_path);
				set_failure(ctx, err);
				return err;
			}
		} else {
			std::ofstream out(args_.safe_output_path, std::ios::binary);
			if (!out.is_open()) {
				std::string err = std::format("Error: Failed to open output path '{}' for writing.", args_.output_path);
				set_failure(ctx, err);
				return err;
			}
			out << md_content;
			if (out.fail()) {
				std::string err = std::format("Error: Failed to write output to '{}'.", args_.output_path);
				set_failure(ctx, err);
				return err;
			}
		}
		set_success(ctx, std::format("Successfully loaded man page for {} and wrote output to '{}'.", args_.name, args_.output_path));
		return "Successfully wrote man page for " + args_.name + " to '" + args_.output_path + "'.";
	}

	set_success(ctx, std::format("Successfully loaded man page for {}", args_.name));
	return fs_utils::wrap_prompt_untrusted_data_tag("fs_man_result", md_content);
}

} // namespace tools
