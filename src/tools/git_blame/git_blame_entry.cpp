#include "../../fs_utils.h"
#include "git_blame.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <ctime>
#include <format>
#include <algorithm>

namespace tools
{

git_blame_tool::git_blame_tool(git_blame_args args)
    : llm_tool_action("Viewing git blame"), args_(std::move(args))
{
}

bool git_blame_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string &out_error) const
{
	if (args_.safe_path.empty()) {
		out_error = "Safe path is not resolved.";
		return false;
	}
	if (!std::filesystem::exists(args_.safe_path)) {
		out_error = "File does not exist: " + args_.requested_path;
		return false;
	}
	if (fs_utils::is_binary_file(args_.safe_path)) {
		out_error = "Cannot run git blame on a binary file: " + args_.requested_path;
		return false;
	}
	return true;
}

struct commit_metadata {
	std::string date = "-";
	std::string summary = "Unknown Commit";
};

struct blame_line_info {
	int line_num;
	std::string commit_hash;
};

struct blame_group {
	int start_line;
	int end_line;
	std::string commit_hash;
};

std::string git_blame_tool::execute(agentlib::tool_context &ctx)
{
	// Read file lines for grounding and bounds validation
	std::vector<std::string> file_lines;
	{
		std::ifstream infile(args_.safe_path);
		if (!infile.is_open()) {
			set_failure(ctx, "Failed to open file");
			return "Failed to open file: " + args_.requested_path;
		}
		std::string line;
		while (std::getline(infile, line)) {
			file_lines.push_back(line);
		}
	}

	if (file_lines.empty()) {
		set_success(ctx, "Empty file");
		return "File is empty: " + args_.requested_path;
	}

	// Clamp start and end line bounds
	int total_lines = (int)file_lines.size();
	int start_line = args_.start_line;
	int end_line = args_.end_line;

	if (start_line <= 0) {
		start_line = 1;
	}
	if (start_line > total_lines) {
		start_line = total_lines;
	}
	if (end_line <= 0 || end_line > total_lines) {
		end_line = total_lines;
	}
	if (start_line > end_line) {
		std::swap(start_line, end_line);
	}

	// Run git blame command using porcelain mode
	std::string cmd = std::format("git --no-pager blame -L {},{} --porcelain -- {}",
		start_line, end_line, fs_utils::escape_shell_arg(args_.safe_path));
	std::string output = fs_utils::execute_command_sync(cmd);

	if (output.find("fatal:") != std::string::npos) {
		set_failure(ctx, "Git blame failed");
		return "Failed: Path is not tracked by Git or not in a Git repository.";
	}

	// Parse porcelain blame output
	std::stringstream ss(output);
	std::string raw_line;
	std::string current_hash;
	std::map<std::string, commit_metadata> commit_cache;
	std::vector<blame_line_info> parsed_lines;

	while (std::getline(ss, raw_line)) {
		if (raw_line.empty()) continue;

		// Source code line starts with a tab character
		if (raw_line[0] == '\t') {
			continue;
		}

		// Metadata key: commit summary (message)
		if (raw_line.rfind("summary ", 0) == 0) {
			if (!current_hash.empty()) {
				commit_cache[current_hash].summary = raw_line.substr(8);
			}
			continue;
		}

		// Metadata key: authoring timestamp
		if (raw_line.rfind("author-time ", 0) == 0) {
			if (!current_hash.empty()) {
				std::string time_str = raw_line.substr(12);
				try {
					long long unixtime = std::stoll(time_str);
					std::time_t temp = unixtime;
					std::tm time_tm;
					if (gmtime_r(&temp, &time_tm)) {
						char date_buf[64];
						std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &time_tm);
						commit_cache[current_hash].date = date_buf;
					}
				} catch (...) {
					// Ignore invalid format, fall back to default
				}
			}
			continue;
		}

		// Check if it's the start of a new line block (40-char hex header)
		if (raw_line.length() >= 40) {
			bool is_hex = true;
			for (int i = 0; i < 40; ++i) {
				char c = raw_line[i];
				if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
					is_hex = false;
					break;
				}
			}
			if (is_hex && (raw_line.length() == 40 || raw_line[40] == ' ')) {
				std::string full_hash = raw_line.substr(0, 40);
				
				// Initialize metadata defaults if first seen
				if (commit_cache.find(full_hash) == commit_cache.end()) {
					if (full_hash == "0000000000000000000000000000000000000000") {
						commit_cache[full_hash] = commit_metadata{ "-", "Not Committed Yet" };
					} else {
						commit_cache[full_hash] = commit_metadata{ "-", "Unknown Commit" };
					}
				}

				// Extract final line number
				std::stringstream line_ss(raw_line.substr(41));
				int result_line = 0;
				if (line_ss >> result_line) {
					blame_line_info info;
					info.line_num = result_line;
					info.commit_hash = full_hash;
					parsed_lines.push_back(info);
				}
				current_hash = full_hash;
			}
		}
	}

	if (parsed_lines.empty()) {
		set_success(ctx, "No blame details found");
		return "No blame information retrieved for: " + args_.requested_path;
	}

	// Sort lines to ensure they are processed chronologically/sequentially by line number
	std::sort(parsed_lines.begin(), parsed_lines.end(), [](const blame_line_info &a, const blame_line_info &b) {
		return a.line_num < b.line_num;
	});

	// Consolidate contiguous line ranges belonging to the same commit hash
	std::vector<blame_group> groups;
	for (const auto &line : parsed_lines) {
		if (groups.empty() || groups.back().commit_hash != line.commit_hash || groups.back().end_line + 1 != line.line_num) {
			blame_group new_group;
			new_group.start_line = line.line_num;
			new_group.end_line = line.line_num;
			new_group.commit_hash = line.commit_hash;
			groups.push_back(new_group);
		} else {
			groups.back().end_line = line.line_num;
		}
	}

	// Generate Markdown Table output
	std::string result = std::format("### Git Blame: {} (Lines {}-{})\n\n", args_.requested_path, start_line, end_line);
	result += "| Line Range | Commit | Date | Grounding Code (First Line) | Commit Description |\n";
	result += "|---|---|---|---|---|\n";

	for (const auto &group : groups) {
		std::string range_str;
		if (group.start_line == group.end_line) {
			range_str = std::to_string(group.start_line);
		} else {
			range_str = std::to_string(group.start_line) + "-" + std::to_string(group.end_line);
		}

		std::string display_hash = group.commit_hash.substr(0, 8);
		std::string date = "-";
		std::string summary = "Unknown Commit";

		auto it = commit_cache.find(group.commit_hash);
		if (it != commit_cache.end()) {
			date = it->second.date;
			summary = it->second.summary;
		}

		std::string grounding_code;
		if (group.start_line >= 1 && group.start_line <= total_lines) {
			grounding_code = file_lines[group.start_line - 1];
		}

		// Escape table separators and backticks
		std::string escaped_code;
		for (char c : grounding_code) {
			if (c == '`') {
				escaped_code += "\\`";
			} else if (c == '|') {
				escaped_code += "\\|";
			} else {
				escaped_code += c;
			}
		}

		// Strip trailing line breaks
		while (!escaped_code.empty() && (escaped_code.back() == '\n' || escaped_code.back() == '\r')) {
			escaped_code.pop_back();
		}

		result += std::format("| {} | `{}` | {} | `{}` | {} |\n", range_str, display_hash, date, escaped_code, summary);
	}

	set_success(ctx, "Blame retrieved");
	return result;
}

} // namespace tools
