#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../config_manager.h"
#include "fs_grep_files.h"
#include "../../fs_utils.h"

namespace fs = std::filesystem;

namespace tools
{

struct last_search_info {
	std::string pattern;
	std::string safe_dir_path;
	std::string include_ext;
};
static last_search_info g_last_search;

fs_grep_files_tool::fs_grep_files_tool(fs_grep_files_args args) : args_(std::move(args))
{
	RE2::Options options;
	compiled_regex_ = std::make_unique<RE2>(args_.pattern, options);
	std::string display_path = "";
	if (args_.dir_path) {
		display_path = *args_.dir_path;
	}
	interaction_ = std::make_shared<agentlib::interaction_fs_grep_files>(args_.pattern, display_path);
}

std::string fs_grep_files_tool::escape_markdown(const std::string &text) const
{
	std::string escaped;
	for (char c : text) {
		if (c == '`' || c == '*' || c == '_' || c == '[' || c == ']') {
			escaped += '\\';
		}
		escaped += c;
	}
	return escaped;
}

bool fs_grep_files_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true; // Directory path is validated in the security stage
}

std::string fs_grep_files_tool::execute(agentlib::tool_context &ctx)
{
	if (!compiled_regex_->ok()) {
		return "Error: Invalid regular expression pattern. " + compiled_regex_->error();
	}

	bool is_duplicate = false;
	std::string curr_ext = "";
	if (args_.include_ext) {
		curr_ext = *args_.include_ext;
	}

	if (g_last_search.pattern == args_.pattern && g_last_search.safe_dir_path == args_.safe_dir_path &&
	    g_last_search.include_ext == curr_ext) {
		is_duplicate = true;
	}

	g_last_search.pattern = args_.pattern;
	g_last_search.safe_dir_path = args_.safe_dir_path;
	g_last_search.include_ext = curr_ext;

	if (is_duplicate) {
		std::string display_path = "project root";
		if (args_.dir_path) {
			display_path = *args_.dir_path;
		}
		return "WARNING: You have already performed this exact search query (fs_grep_files with pattern: \"" + args_.pattern +
		       "\" in " + display_path +
		       "). Repeating the same query yields the same results. To find what you are looking for, please refine your search "
		       "pattern, search in a different directory, or read the files containing matches directly.";
	}

	std::string build_dir = config_manager::get_instance().get_build_directory();
	fs::path root_path = ctx.fs_security.get_working_directory();

	// Use the resolved, safe starting path
	fs::path search_path(args_.safe_dir_path);

	std::set<std::string> open_files;
	if (ctx.doc_provider) {
		auto paths = ctx.doc_provider->get_open_document_paths();
		for (const auto &p : paths) {
			open_files.insert(p);
		}
	}

	int total_detailed_matches = 0;
	std::map<std::string, std::vector<std::pair<int, std::string>>> detailed_matches;
	std::set<std::string> overflow_files;

	try {
		for (auto it = fs::recursive_directory_iterator(search_path, fs::directory_options::skip_permission_denied);
		     it != fs::recursive_directory_iterator(); ++it) {

			const auto &path = it->path();

			if (it->is_directory()) {
				std::string name = path.filename().string();
				bool is_top_level = !path.parent_path().has_relative_path() || path.parent_path() == root_path;

				// Skip hidden dirs, build dirs, and tmp/temp
				if (name.front() == '.' || name == build_dir || name == "tmp" || name == "temp" ||
				    (is_top_level && name.starts_with("build"))) {
					it.disable_recursion_pending();
				}
				continue;
			}

			if (!fs::is_regular_file(path)) {
				continue;
			}

			if (args_.include_ext) {
				if (path.extension().string() != *args_.include_ext) {
					continue;
				}
			}

			std::string abs_path_str = path.string();
			std::string rel_path_str = fs::relative(path, root_path).string();

			std::vector<std::string> file_lines;
			bool read_success = false;

			// 1. Check if the file is an open editor buffer
			if (open_files.contains(abs_path_str) && ctx.doc_provider) {
				auto snapshot = ctx.doc_provider->get_open_document(abs_path_str);
				if (snapshot) {
					for (size_t i = 0; i < snapshot->get_line_count(); ++i) {
						file_lines.push_back(snapshot->get_line_text(i));
					}
					read_success = true;
				}
			}
			// 2. Fallback to direct disk read
			else if (!fs_utils::is_binary_file(abs_path_str)) {
				struct stat sb;
				if (stat(abs_path_str.c_str(), &sb) == 0 && sb.st_size > 0 && sb.st_size < 50 * 1024 * 1024) {
					std::ifstream file(abs_path_str, std::ios::binary);
					if (file) {
						std::string buffer(sb.st_size, ' ');
						if (file.read(buffer.data(), sb.st_size)) {
							std::string line;
							std::istringstream iss(buffer);
							while (std::getline(iss, line)) {
								if (!line.empty() && line.back() == '\r') {
									line.pop_back();
								}
								file_lines.push_back(line);
							}
							read_success = true;
						}
					}
				}
			}

			if (read_success) {
				std::vector<int> match_lines;
				for (size_t i = 0; i < file_lines.size(); ++i) {
					if (RE2::PartialMatch(file_lines[i], *compiled_regex_)) {
						if (total_detailed_matches < args_.max_results) {
							match_lines.push_back(i + 1);
							total_detailed_matches++;
						} else {
							overflow_files.insert(rel_path_str);
							break;
						}
					}
				}

				if (!match_lines.empty()) {
					auto &matches = detailed_matches[rel_path_str];

					// Merge overlapping match blocks
					std::vector<std::pair<int, int>> merged_blocks; // start_line, end_line (1-based)
					for (int match_line : match_lines) {
						int block_start = std::max(1, match_line - args_.context_lines);
						int block_end =
						    std::min(static_cast<int>(file_lines.size()), match_line + args_.context_lines);

						if (merged_blocks.empty()) {
							merged_blocks.push_back({block_start, block_end});
						} else {
							auto &last_block = merged_blocks.back();
							if (block_start <= last_block.second + 1) { // Overlaps or is adjacent
								last_block.second = std::max(last_block.second, block_end);
							} else {
								merged_blocks.push_back({block_start, block_end});
							}
						}
					}

					// Format blocks
					for (const auto &block : merged_blocks) {
						std::stringstream block_ss;
						for (int i = block.first; i <= block.second; ++i) {
							block_ss << i << ": " << file_lines[i - 1] << "\n";
						}
						matches.push_back({block.first, block_ss.str()});
					}
				}
			}

			// If we've hit max results, we could keep scanning to populate overflow_files.
			// For massive codebases, finding ALL files might take a while, but it's useful.
			// If performance becomes an issue, we could cap overflow_files at e.g. 100.
			if (overflow_files.size() > 50) {
				break; // Hard cap on overflow files to prevent infinite hangs
			}
		}
	} catch (const std::exception &e) {
		return "Error during search traversal: " + std::string(e.what());
	}

	if (detailed_matches.empty() && overflow_files.empty()) {
		return "No matches found.";
	}

	std::stringstream ss;
	ss << "Found " << total_detailed_matches;
	if (!overflow_files.empty()) {
		ss << "+";
	}
	ss << " matches across " << (detailed_matches.size() + overflow_files.size()) << " files:\n\n";

	for (const auto &[file, matches] : detailed_matches) {
		ss << "### `" << file << "`\n";
		for (const auto &match : matches) {
			std::string content = match.second;
			// Truncate excessively long blocks to protect context window, but be generous for context
			if (content.length() > 2000) {
				content = content.substr(0, 1997) + "...";
			}

			if (args_.context_lines == 0) {
				// Legacy bullet-point format for 0 context lines to save tokens
				std::string single_line = content;
				if (!single_line.empty() && single_line.back() == '\n')
					single_line.pop_back(); // Remove trailing newline
				size_t colon_pos = single_line.find(": ");
				if (colon_pos != std::string::npos) {
					single_line = single_line.substr(colon_pos + 2);
				}
				ss << "* **Line " << match.first << ":** `" << escape_markdown(single_line) << "`\n";
			} else {
				// New multi-line block format
				ss << "**Match near Line " << match.first << ":**\n";
				// Optionally extract extension for syntax highlighting
				std::string ext = "";
				size_t dot_pos = file.find_last_of('.');
				if (dot_pos != std::string::npos && dot_pos < file.length() - 1) {
					ext = file.substr(dot_pos + 1);
				}
				ss << "```" << ext << "\n" << content;
				if (!content.empty() && content.back() != '\n')
					ss << "\n";
				ss << "```\n";
			}
		}
		ss << "\n";
	}

	if (!overflow_files.empty()) {
		ss << "---\n";
		ss << "*Note: `max_results` (" << args_.max_results
		   << ") limit reached. Additional matches were found in the following files. Consider narrowing your search or specifying "
		      "a `dir_path`.*\n";
		for (const auto &f : overflow_files) {
			ss << "- `" << f << "`\n";
		}
	}

	std::string result_str = ss.str();
	interaction_->set_result(result_str);
	if (ctx.trigger_ui_update) {
		ctx.trigger_ui_update();
	}

	return result_str;
}

} // namespace tools