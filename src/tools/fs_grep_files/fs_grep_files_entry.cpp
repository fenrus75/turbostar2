#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config_manager.h"
#include "fs_grep_files.h"
#include "mime.h"
#include "codemap_utils.h"
#include "fs_utils.h"
#include "project_manager.h"
#include <format>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace tools
{

static std::string symbol_kind_to_string(int kind)
{
	switch (kind) {
		case 1: return "File";
		case 2: return "Module";
		case 3: return "Namespace";
		case 4: return "Package";
		case 5: return "Class";
		case 6: return "Method";
		case 7: return "Property";
		case 8: return "Field";
		case 9: return "Constructor";
		case 10: return "Enum";
		case 11: return "Interface";
		case 12: return "Function";
		case 13: return "Variable";
		case 14: return "Constant";
		case 15: return "String";
		case 16: return "Number";
		case 17: return "Boolean";
		case 18: return "Array";
		case 19: return "Object";
		case 20: return "Key";
		case 21: return "Null";
		case 22: return "EnumMember";
		case 23: return "Struct";
		case 24: return "Event";
		case 25: return "Operator";
		case 26: return "TypeParameter";
		default: return "Symbol";
	}
}

static bool is_exact_or_suffix_match(const std::string &sym_name, const std::string &query)
{
	if (sym_name == query) {
		return true;
	}
	if (sym_name.length() > query.length() + 2) {
		if (sym_name.compare(sym_name.length() - query.length(), query.length(), query) == 0 &&
		    sym_name.compare(sym_name.length() - query.length() - 2, 2, "::") == 0) {
			return true;
		}
	}
	return false;
}

static bool contains_case_insensitive(const std::string &haystack, const std::string &needle)
{
	if (needle.empty()) return true;
	auto it = std::search(
		haystack.begin(), haystack.end(),
		needle.begin(), needle.end(),
		[](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
	);
	return it != haystack.end();
}

static bool is_source_extension(std::string_view ext)
{
	return ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c" || ext == ".cc" ||
	       ext == ".cxx" || ext == ".py" || ext == ".js" || ext == ".ts" || ext == ".rs" ||
	       ext == ".go" || ext == ".java" || ext == ".cs" || ext == ".rb" || ext == ".sh";
}

static int calculate_file_priority_tier(const fs::path &path, bool is_open_buffer)
{
	std::string filename = path.filename().string();
	std::string path_str = path.string();

	// Tier 4: Backup / Temporary Files / Swp / Tilde files
	if (!filename.empty() && (filename.back() == '~' || filename.ends_with(".bak") || filename.ends_with(".orig") ||
	    filename.ends_with(".swp") || filename.starts_with("#") || filename.ends_with("#"))) {
		return 4;
	}

	// Tier 3: Build outputs, vendor headers, log files
	if (path_str.contains("/build/") || path_str.starts_with("build/") ||
	    filename.ends_with(".log") || filename.ends_with(".out") || path_str.starts_with("/usr/")) {
		return 3;
	}

	// Tier 1: Open buffers or primary source files
	if (is_open_buffer || is_source_extension(path.extension().string())) {
		return 1;
	}

	// Tier 2: General project files / docs / configs
	return 2;
}

struct last_search_info {
	std::string pattern;
	std::string safe_search_path;
	std::string include_ext;
	std::string exclude_path;
	std::string exclude_ext;
	std::string exclude_pattern;
	bool is_regex{false};
	bool case_insensitive{false};
};

/*
 * g_last_search_mutex protects the g_last_search static state used for duplicate search detection.
 * Locking rules: Acquire g_last_search_mutex before reading or updating g_last_search.
 */
static std::mutex g_last_search_mutex;
static last_search_info g_last_search;

std::vector<lsp_manager::symbol_info> fs_grep_files_tool::get_lsp_symbols(const std::string &query)
{
	return project_manager::get_instance().lsp_query_workspace_symbols(query);
}

fs_grep_files_tool::fs_grep_files_tool(fs_grep_files_args args) : args_(std::move(args))
{
	RE2::Options options;
	if (args_.case_insensitive) {
		options.set_case_sensitive(false);
	}
	std::string final_pattern = args_.is_regex ? args_.pattern : RE2::QuoteMeta(args_.pattern);
	compiled_regex_ = std::make_unique<RE2>(final_pattern, options);

	if (args_.exclude_pattern && !args_.exclude_pattern->empty()) {
		compiled_exclude_regex_ = std::make_unique<RE2>(*args_.exclude_pattern, options);
	}

	std::string display_path = "";
	if (args_.search_path) {
		display_path = *args_.search_path;
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

	if (!args_.pattern.empty()) {
		auto &patterns = ctx.recent_grep_patterns;
		patterns.erase(std::remove(patterns.begin(), patterns.end(), args_.pattern), patterns.end());
		patterns.push_front(args_.pattern);
		if (patterns.size() > 5) {
			patterns.pop_back();
		}
	}

	bool is_duplicate = false;
	std::string curr_ext = args_.include_ext ? *args_.include_ext : "";
	std::string curr_ex_path = args_.exclude_path ? *args_.exclude_path : "";
	std::string curr_ex_ext = args_.exclude_ext ? *args_.exclude_ext : "";
	std::string curr_ex_pat = args_.exclude_pattern ? *args_.exclude_pattern : "";

	{
		std::lock_guard<std::mutex> lock(g_last_search_mutex);
		if (g_last_search.pattern == args_.pattern && g_last_search.safe_search_path == args_.safe_search_path &&
		    g_last_search.include_ext == curr_ext && g_last_search.exclude_path == curr_ex_path &&
		    g_last_search.exclude_ext == curr_ex_ext && g_last_search.exclude_pattern == curr_ex_pat &&
		    g_last_search.is_regex == args_.is_regex && g_last_search.case_insensitive == args_.case_insensitive) {
			is_duplicate = true;
		}

		g_last_search.pattern = args_.pattern;
		g_last_search.safe_search_path = args_.safe_search_path;
		g_last_search.include_ext = curr_ext;
		g_last_search.exclude_path = curr_ex_path;
		g_last_search.exclude_ext = curr_ex_ext;
		g_last_search.exclude_pattern = curr_ex_pat;
		g_last_search.is_regex = args_.is_regex;
		g_last_search.case_insensitive = args_.case_insensitive;
	}

	if (is_duplicate) {
		std::string display_path = "project root";
		if (args_.search_path) {
			display_path = *args_.search_path;
		}
		return "WARNING: You have already performed this exact search query (fs_grep_files with pattern: \"" + args_.pattern +
		       "\" in " + display_path +
		       "). Repeating the same query yields the same results. To find what you are looking for, please refine your search "
		       "pattern, search in a different directory, or read the files containing matches directly.";
	}

	std::string build_dir = config_manager::get_instance().get_build_directory();
	fs::path root_path = ctx.fs_security.get_working_directory();

	// Use the resolved, safe starting path
	fs::path search_path(args_.safe_search_path);

	std::string lsp_section;
	bool is_regular_word = !args_.is_regex && !args_.pattern.empty() &&
		std::all_of(args_.pattern.begin(), args_.pattern.end(), [](char c) {
			return std::isalnum(c) || c == '_';
		});

	if (is_regular_word) {
		std::vector<lsp_manager::symbol_info> raw_symbols = get_lsp_symbols(args_.pattern);
		std::vector<lsp_manager::symbol_info> exact_matches;
		std::vector<lsp_manager::symbol_info> other_matches;

		for (const auto &sym : raw_symbols) {
			if (sym.location.path.empty()) {
				continue;
			}
			if (is_exact_or_suffix_match(sym.name, args_.pattern)) {
				exact_matches.push_back(sym);
			} else if (contains_case_insensitive(sym.name, args_.pattern)) {
				other_matches.push_back(sym);
			}
		}

		// Combine prioritised matches, cap at 5
		std::vector<lsp_manager::symbol_info> final_symbols;
		final_symbols.insert(final_symbols.end(), exact_matches.begin(), exact_matches.end());
		if (final_symbols.size() < 5) {
			for (const auto &sym : other_matches) {
				if (final_symbols.size() >= 5) break;
				final_symbols.push_back(sym);
			}
		}

		if (!final_symbols.empty()) {
			std::stringstream lsp_ss;
			lsp_ss << "### LSP Symbol Definitions:\n";
			for (const auto &sym : final_symbols) {
				std::string kind_str = symbol_kind_to_string(sym.kind);
				fs::path abs_path(sym.location.path);
				std::string display_path = fs_utils::make_relative_to_project(sym.location.path, root_path.string());

				
				int start_line = sym.location.range.start_y + 1;
				int end_line = sym.location.range.end_y + 1;

				if (end_line <= start_line) {
					std::string safe_file_path;
					std::string out_err;
					fs::path full_path = abs_path.is_absolute() ? abs_path : (root_path / abs_path);
					if (ctx.fs_security.validate_access(full_path.string(), agentlib::access_type::read, safe_file_path, out_err)) {
						auto file_symbols = get_document_codemap_symbols(safe_file_path, ctx, /*min_lines=*/1);
						for (const auto &csym : file_symbols) {
							if (csym.start_line <= start_line && csym.end_line >= start_line) {
								if (csym.end_line > start_line) {
									end_line = csym.end_line;
									break;
								}
							}
						}
					}
				}

				if (end_line > start_line) {
					lsp_ss << std::format("* **{} `{}`** is defined in `{}` at line {} to {}\n",
						kind_str, sym.name, display_path, start_line, end_line);
				} else {
					lsp_ss << std::format("* **{} `{}`** is defined in `{}` at line {}\n",
						kind_str, sym.name, display_path, start_line);
				}
			}
			lsp_ss << "\n---\n\n";
			lsp_section = lsp_ss.str();
		}
	}

	std::set<std::string> open_files;
	if (ctx.doc_provider) {
		auto paths = ctx.doc_provider->get_open_document_paths();
		for (const auto &p : paths) {
			open_files.insert(p);
		}
	}

	struct raw_file_match {
		int tier{1};
		std::string rel_path_str;
		std::vector<int> match_lines;
		std::vector<std::string> file_lines;
	};

	std::vector<raw_file_match> raw_matches;

	auto is_file_excluded = [&](const fs::path &path, const std::string &rel_path_str) {
		std::string path_str = path.string();
		std::string filename = path.filename().string();
		std::string ext = path.extension().string();

		if (args_.exclude_path && !args_.exclude_path->empty()) {
			const std::string &ex_path = *args_.exclude_path;
			if (path_str.contains(ex_path) || rel_path_str.contains(ex_path)) {
				return true;
			}
		}

		if (args_.exclude_ext && !args_.exclude_ext->empty()) {
			const std::string &ex_ext = *args_.exclude_ext;
			std::stringstream ss(ex_ext);
			std::string token;
			while (std::getline(ss, token, ',')) {
				size_t first = token.find_first_not_of(" \t");
				if (first == std::string::npos) continue;
				size_t last = token.find_last_not_of(" \t");
				token = token.substr(first, last - first + 1);
				if (token.empty()) continue;
				if (token.front() != '.') token = "." + token;
				if (ext == token || filename.ends_with(token)) {
					return true;
				}
			}
		}

		if (compiled_exclude_regex_ && compiled_exclude_regex_->ok()) {
			if (RE2::PartialMatch(rel_path_str, *compiled_exclude_regex_) ||
			    RE2::PartialMatch(path_str, *compiled_exclude_regex_)) {
				return true;
			}
		}

		return false;
	};

	int total_detailed_matches = 0;
	std::map<std::string, std::vector<std::pair<int, std::string>>> detailed_matches;
	std::set<std::string> overflow_files;

	try {
		auto process_file = [&](const fs::path &path) {
			std::string abs_path_str = path.string();
			std::string safe_file_path;
			std::string access_err;
			if (!ctx.fs_security.validate_access(abs_path_str, agentlib::access_type::read, safe_file_path, access_err)) {
				return;
			}

			std::string rel_path_str = fs::relative(path, root_path).string();

			if (is_file_excluded(path, rel_path_str)) {
				return;
			}

			if (args_.include_ext && path.extension().string() != *args_.include_ext) {
				return;
			}

			std::vector<std::string> file_lines;
			bool read_success = false;
			bool is_open_buffer = open_files.contains(abs_path_str);

			// 1. Check if the file is an open editor buffer
			if (is_open_buffer && ctx.doc_provider) {
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
						match_lines.push_back(static_cast<int>(i + 1));
					}
				}

				if (!match_lines.empty()) {
					int tier = calculate_file_priority_tier(path, is_open_buffer);
					raw_matches.push_back({tier, rel_path_str, std::move(match_lines), std::move(file_lines)});
				}
			}
		};

		auto vfs = ctx.fs_security.get_vfs();
		std::string raw_search_path = args_.safe_search_path;
		if (raw_search_path.empty() && args_.search_path) {
			raw_search_path = *args_.search_path;
		}

		bool is_vfs = (raw_search_path.find("://") != std::string::npos);
		if (!is_vfs && vfs && !fs::exists(raw_search_path) && vfs->exists(raw_search_path)) {
			is_vfs = true;
		}

		if (is_vfs && vfs) {
			std::vector<std::string> file_uris;
			std::function<void(const std::string &)> collect_uris = [&](const std::string &uri) {
				auto info = vfs->get_file_info(uri);
				if (info && info->type == 'F') {
					file_uris.push_back(uri);
					return;
				}
				auto entries = vfs->list_directory(uri);
				for (const auto &entry : entries) {
					if (entry.type == 'F') {
						file_uris.push_back(entry.uri);
					} else if (entry.type == 'D') {
						collect_uris(entry.uri);
					}
				}
			};

			collect_uris(raw_search_path);

			for (const auto &file_uri : file_uris) {
				fs::path p(file_uri);
				if (is_file_excluded(p, file_uri)) {
					continue;
				}
				if (args_.include_ext) {
					if (p.extension().string() != *args_.include_ext) {
						continue;
					}
				}

				auto handle = vfs->read_file(file_uri);
				if (!handle) {
					continue;
				}

				std::string content = std::string((*handle)->view());
				std::vector<std::string> file_lines;
				std::string line;
				std::istringstream iss(content);
				while (std::getline(iss, line)) {
					if (!line.empty() && line.back() == '\r') {
						line.pop_back();
					}
					file_lines.push_back(line);
				}

				std::vector<int> match_lines;
				for (size_t i = 0; i < file_lines.size(); ++i) {
					if (RE2::PartialMatch(file_lines[i], *compiled_regex_)) {
						match_lines.push_back(static_cast<int>(i + 1));
					}
				}

				if (!match_lines.empty()) {
					int tier = calculate_file_priority_tier(p, false);
					raw_matches.push_back({tier, file_uri, std::move(match_lines), std::move(file_lines)});
				}
			}
		} else if (fs::is_regular_file(search_path)) {
			process_file(search_path);
		} else {
			for (auto it = fs::recursive_directory_iterator(search_path, fs::directory_options::skip_permission_denied);
			     it != fs::recursive_directory_iterator(); ++it) {

				const auto &path = it->path();

				if (it->is_directory()) {
					std::string name = path.filename().string();
					std::string rel_p = fs::relative(path, root_path).string();
					bool is_top_level = !path.parent_path().has_relative_path() || path.parent_path() == root_path;

					// Skip hidden dirs, build dirs, tmp/temp, and explicit exclude_path matches
					if (name.front() == '.' || name == build_dir || name == "tmp" || name == "temp" ||
					    (is_top_level && name.starts_with("build")) ||
					    (args_.exclude_path && !args_.exclude_path->empty() && (path.string().contains(*args_.exclude_path) || rel_p.contains(*args_.exclude_path)))) {
						it.disable_recursion_pending();
					}
					continue;
				}

				if (!fs::is_regular_file(path)) {
					continue;
				}

				process_file(path);
			}
		}

		std::stable_sort(raw_matches.begin(), raw_matches.end(), [](const raw_file_match &a, const raw_file_match &b) {
			if (a.tier != b.tier) {
				return a.tier < b.tier; // Tier 1 (core) first, Tier 4 (backup ~) last
			}
			return a.rel_path_str < b.rel_path_str;
		});

		for (const auto &fm : raw_matches) {
			std::vector<int> match_lines_detailed;
			for (int match_line : fm.match_lines) {
				if (total_detailed_matches < args_.limit) {
					match_lines_detailed.push_back(match_line);
					total_detailed_matches++;
				} else {
					overflow_files.insert(fm.rel_path_str);
				}
			}

			if (!match_lines_detailed.empty()) {
				auto &matches = detailed_matches[fm.rel_path_str];

				// Merge overlapping match blocks
				std::vector<std::pair<int, int>> merged_blocks; // start_line, end_line (1-based)
				for (int match_line : match_lines_detailed) {
					int block_start = std::max(1, match_line - args_.context_lines);
					int block_end =
					    std::min(static_cast<int>(fm.file_lines.size()), match_line + args_.context_lines);

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
						block_ss << i << ": " << fm.file_lines[i - 1] << "\n";
					}
					matches.push_back({block.first, block_ss.str()});
				}
			}
		}
	} catch (const std::exception &e) {
		return "Error during search traversal: " + std::string(e.what());
	}

	if (detailed_matches.empty() && overflow_files.empty() && lsp_section.empty()) {
		return "No matches found.";
	}

	std::string result_str;
	if (detailed_matches.empty() && overflow_files.empty()) {
		result_str = lsp_section + "No matches found.";
	} else {
		std::stringstream ss;
		ss << "Found " << total_detailed_matches;
		if (!overflow_files.empty()) {
			ss << "+";
		}
		ss << " matches across " << (detailed_matches.size() + overflow_files.size()) << " files:\n\n";

		for (const auto &[file, matches] : detailed_matches) {
			ss << "### `" << file << "`\n";

			// Query document symbols for enclosing scope annotation
			std::vector<codemap_symbol_info> file_symbols;
			std::string safe_file_path;
			std::string out_err;
			if (ctx.fs_security.validate_access((root_path / file).string(), agentlib::access_type::read, safe_file_path, out_err)) {
				file_symbols = get_document_codemap_symbols(safe_file_path, ctx, /*min_lines=*/1);
			}

			for (const auto &match : matches) {
				std::string content = match.second;
				// Truncate excessively long blocks to protect context window, but be generous for context
				if (content.length() > 2000) {
					content = content.substr(0, 1997) + "...";
				}

				std::string scope_hint;
				if (!file_symbols.empty()) {
					const codemap_symbol_info *enc_sym = find_enclosing_symbol(file_symbols, match.first);
					if (enc_sym) {
						if (enc_sym->end_line > enc_sym->start_line) {
							scope_hint = std::format(" [in {} `{}` L{}-{}]", enc_sym->kind_str, enc_sym->name, enc_sym->start_line, enc_sym->end_line);
						} else {
							scope_hint = std::format(" [in {} `{}` L{}]", enc_sym->kind_str, enc_sym->name, enc_sym->start_line);
						}
					}
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
					ss << "* **Line " << match.first << "**" << scope_hint << ": `" << escape_markdown(single_line) << "`\n";
				} else {
					// New multi-line block format
					ss << "**Match near Line " << match.first << "**" << scope_hint << ":\n";
					// Optionally extract extension/language for syntax highlighting
					std::string ext = mime::get_language_from_extension(file);
					if (ext.empty()) {
						size_t dot_pos = file.find_last_of('.');
						if (dot_pos != std::string::npos && dot_pos < file.length() - 1) {
							ext = file.substr(dot_pos + 1);
						}
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
			ss << "*Note: `limit` (" << args_.limit
			   << ") limit reached. Additional matches were found in the following files. Consider narrowing your search or specifying "
			      "a `search_path`.*\n";
			for (const auto &f : overflow_files) {
				ss << "- `" << f << "`\n";
			}
		}
		result_str = lsp_section + ss.str();
	}

	interaction_->set_result(result_str);
	if (ctx.trigger_ui_update) {
		ctx.trigger_ui_update();
	}

	return fs_utils::wrap_prompt_untrusted_data_tag("fs_grep_files_result", result_str);
}

} // namespace tools
