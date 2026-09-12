#include "call_resolver.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>

#include "call_token_extractor.h"
#include "fs_utils.h"
#include "project_manager.h"

namespace tools
{

namespace
{

bool is_matching_function_symbol(const codemap_symbol_info *sym, std::string_view name)
{
	if (!sym) {
		return false;
	}
	if (sym->kind_str.find("Class") != std::string::npos || sym->kind_str.find("Struct") != std::string::npos ||
	    sym->kind_str.find("Namespace") != std::string::npos) {
		return false;
	}
	if (sym->name == name || sym->display_name == name) {
		return true;
	}
	if (sym->name.ends_with("::" + std::string(name))) {
		return true;
	}
	return false;
}

} // namespace

bool call_resolver::is_project_file(std::string_view path, agentlib::tool_context * /*ctx*/)
{
	if (path.empty()) {
		return false;
	}
	if (path.starts_with("file://")) {
		path.remove_prefix(7);
	}
	if (path.empty()) {
		return false;
	}

	std::filesystem::path abs_p = fs_utils::safe_absolute(std::string(path));
	std::string proj_root = project_manager::get_instance().get_project_root();
	if (proj_root.empty()) {
		proj_root = fs_utils::get_project_dir();
	}
	if (!proj_root.empty()) {
		std::filesystem::path root_p = fs_utils::safe_absolute(proj_root);
		std::error_code ec;
		auto rel_p = std::filesystem::relative(abs_p, root_p, ec);
		if (!ec && !rel_p.empty() && rel_p.string() != "." && !rel_p.string().starts_with("..") && !rel_p.is_absolute()) {
			return true;
		}
	}

	if (path.starts_with("/usr/") || path.starts_with("/opt/") || path.starts_with("/lib/") || path.starts_with("/tmp/") ||
	    path.starts_with("/etc/") || path.starts_with("/var/")) {
		return false;
	}
	if (path.find("/bits/") != std::string_view::npos ||
	    path.find("gcc/") != std::string_view::npos || path.find("clang/") != std::string_view::npos) {
		return false;
	}

	std::string rel = fs_utils::make_relative_to_project(abs_p.string());
	if (rel.empty() || rel.starts_with("/") || rel.starts_with("inc://") || rel.starts_with("..")) {
		return false;
	}
	return true;
}

bool call_resolver::is_test_path(std::string_view p)
{
	if (p.starts_with("test/") || p.starts_with("tests/") ||
	    p.starts_with("selftest/") || p.starts_with("selftests/") ||
	    p.starts_with("tools/testing/")) {
		return true;
	}
	if (p.find("/test/") != std::string_view::npos || p.find("/tests/") != std::string_view::npos ||
	    p.find("/selftest/") != std::string_view::npos || p.find("/selftests/") != std::string_view::npos ||
	    p.find("tools/testing/") != std::string_view::npos) {
		return true;
	}
	return false;
}

std::unordered_set<std::string> call_resolver::extract_included_headers(const std::string &filepath)
{
	std::unordered_set<std::string> includes;
	std::ifstream file(filepath);
	if (!file.is_open()) {
		return includes;
	}

	static const std::regex inc_regex(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
	std::string line;
	int line_count = 0;

	while (std::getline(file, line) && line_count < 1000) {
		line_count++;
		std::smatch match;
		if (std::regex_search(line, match, inc_regex)) {
			if (match.size() > 1) {
				includes.insert(match[1].str());
			}
		}
	}

	return includes;
}

std::optional<lsp_backend::location_info> call_resolver::disambiguate_locations(
	std::span<const lsp_backend::location_info> locations,
	std::string_view caller_file,
	agentlib::tool_context *ctx)
{
	if (locations.empty()) {
		return std::nullopt;
	}

	std::string norm_caller = fs_utils::make_relative_to_project(caller_file);
	bool caller_is_test = is_test_path(norm_caller);

	// Filter down to valid project locations and exclude test files if caller is not a test
	std::vector<lsp_backend::location_info> valid;
	for (const auto &loc : locations) {
		if (loc.path.empty() || !is_project_file(loc.path, ctx)) {
			continue;
		}
		std::string norm_loc = fs_utils::make_relative_to_project(loc.path);
		if (norm_loc.empty() || norm_loc == norm_caller) {
			continue;
		}
		if (!caller_is_test && is_test_path(norm_loc)) {
			continue;
		}
		valid.push_back(loc);
	}

	if (valid.empty()) {
		return std::nullopt;
	}
	if (valid.size() == 1) {
		return valid.front();
	}

	// Check if all remaining locations point to the same file
	std::string first_file = fs_utils::make_relative_to_project(valid.front().path);
	bool all_same_file = true;
	for (size_t i = 1; i < valid.size(); ++i) {
		if (fs_utils::make_relative_to_project(valid[i].path) != first_file) {
			all_same_file = false;
			break;
		}
	}
	if (all_same_file) {
		return valid.front();
	}

	// Context-aware scoring: evaluate caller's included headers and subsystem
	std::unordered_set<std::string> included_headers = extract_included_headers(std::string(caller_file));
	std::filesystem::path caller_path(norm_caller);
	std::string caller_dir = caller_path.parent_path().string();

	struct scored_candidate {
		const lsp_backend::location_info *loc;
		int score;
	};

	std::vector<scored_candidate> scored;
	scored.reserve(valid.size());

	for (const auto &cand : valid) {
		int score = 0;
		std::string norm_cand = fs_utils::make_relative_to_project(cand.path);
		std::filesystem::path cand_p(norm_cand);
		std::string cand_filename = cand_p.filename().string();

		// 1. Direct header inclusion match
		for (const auto &inc : included_headers) {
			if (norm_cand.ends_with(inc)) {
				score += 100;
				break;
			}
			std::filesystem::path inc_p(inc);
			if (inc_p.filename().string() == cand_filename) {
				score += 50;
				break;
			}
		}

		// 2. Subsystem / directory match
		if (!caller_dir.empty() && norm_cand.starts_with(caller_dir + "/")) {
			score += 40;
		}

		// 3. Generic include directories (e.g. include/linux/, include/)
		if (norm_cand.starts_with("include/linux/") || norm_cand.starts_with("include/")) {
			score += 30;
		}

		// 4. Architecture penalty if caller is not in that architecture
		if (norm_cand.starts_with("arch/")) {
			if (!norm_caller.starts_with("arch/")) {
				score -= 40;
			} else {
				// Both in arch: check if matching architecture (e.g. arch/x86 vs arch/arm)
				size_t caller_arch_slash = norm_caller.find('/', 5);
				size_t cand_arch_slash = norm_cand.find('/', 5);
				if (caller_arch_slash != std::string::npos && cand_arch_slash != std::string::npos) {
					std::string caller_arch = norm_caller.substr(0, caller_arch_slash);
					std::string cand_arch = norm_cand.substr(0, cand_arch_slash);
					if (caller_arch == cand_arch) {
						score += 40;
					} else {
						score -= 40;
					}
				}
			}
		}

		scored.push_back({&cand, score});
	}

	std::sort(scored.begin(), scored.end(), [](const scored_candidate &a, const scored_candidate &b) {
		return a.score > b.score;
	});

	// If the top candidate scored significantly higher than the runner-up, take it!
	if (scored.size() == 1 || scored[0].score >= scored[1].score + 20) {
		return *scored[0].loc;
	}

	// Tie without sufficient context: fail safely
	return std::nullopt;
}

bool call_resolver::resolve_target(
	outgoing_call_reference &ref,
	const lsp_manager::call_hierarchy_item &item,
	std::unordered_map<std::string, std::vector<codemap_symbol_info>> &symbols_cache,
	agentlib::tool_context *ctx)
{
	std::string target_uri_path = fs_utils::make_relative_to_project(item.uri);
	std::string norm_caller = fs_utils::make_relative_to_project(ref.caller_file);
	bool caller_is_test = is_test_path(norm_caller);

	if (!target_uri_path.empty() && !is_project_file(item.uri, ctx)) {
		return false;
	}
	if (!caller_is_test && !target_uri_path.empty() && is_test_path(target_uri_path)) {
		return false;
	}

	std::string def_path;
	int def_pos_line = -1;

	// 1. Primary resolution: ask LSP for definition location of the symbol
	if (item.selection_range.start_y >= 0 && item.selection_range.start_x >= 0 && !target_uri_path.empty()) {
		auto defs = project_manager::get_instance().lsp_query_definition(
			target_uri_path, item.selection_range.start_y, item.selection_range.start_x);

		auto chosen = disambiguate_locations(defs, ref.caller_file, ctx);
		if (chosen) {
			def_path = fs_utils::make_relative_to_project(chosen->path);
			def_pos_line = chosen->range.start_y + 1;
		}
	}

	// If URI was empty (e.g. semcode returned callee name only), attempt workspace symbol query
	if (def_path.empty() && !item.name.empty()) {
		auto ws_syms = project_manager::get_instance().lsp_query_workspace_symbols(item.name);
		std::vector<lsp_backend::location_info> locs;
		for (const auto &ws : ws_syms) {
			if (ws.name == item.name) {
				locs.push_back(ws.location);
			}
		}
		auto chosen = disambiguate_locations(locs, ref.caller_file, ctx);
		if (chosen) {
			def_path = fs_utils::make_relative_to_project(chosen->path);
			def_pos_line = chosen->range.start_y + 1;
		}
	}

	// 2. Fallback resolution: if def_path is a header, check if matching .cpp exists
	if (def_path.empty() && !target_uri_path.empty()) {
		if (target_uri_path.ends_with(".h") || target_uri_path.ends_with(".hpp")) {
			std::string impl = find_matching_impl_file(target_uri_path, ctx);
			if (!impl.empty()) {
				if (!symbols_cache.contains(impl)) {
					std::vector<codemap_symbol_info> syms;
					fallback_find_symbols(impl, 1, syms);
					symbols_cache[impl] = std::move(syms);
				}
				const auto &impl_syms = symbols_cache[impl];
				if (find_symbol_by_hint(impl_syms, item.name)) {
					def_path = impl;
				}
			}
		}
		if (def_path.empty()) {
			if (!caller_is_test && is_test_path(target_uri_path)) {
				return false;
			}
			if (target_uri_path.ends_with(".c") && norm_caller.ends_with(".c") && target_uri_path != norm_caller) {
				return false;
			}
			def_path = target_uri_path;
		}
	}

	if (def_path.empty()) {
		return false;
	}

	// 3. Factual symbol bounds from dedicated codemap of def_path
	if (!symbols_cache.contains(def_path)) {
		std::vector<codemap_symbol_info> syms;
		fallback_find_symbols(def_path, 1, syms);
		symbols_cache[def_path] = std::move(syms);
	}
	const auto &target_syms = symbols_cache[def_path];
	const codemap_symbol_info *found = nullptr;
	if (def_pos_line > 0) {
		const codemap_symbol_info *encl = find_enclosing_symbol(target_syms, def_pos_line);
		if (is_matching_function_symbol(encl, item.name)) {
			found = encl;
		}
	}
	if (!found) {
		const codemap_symbol_info *hint_sym = find_symbol_by_hint(target_syms, item.name);
		if (is_matching_function_symbol(hint_sym, item.name)) {
			found = hint_sym;
		}
	}

	// Check matching implementation if header
	if (!found && (def_path.ends_with(".h") || def_path.ends_with(".hpp"))) {
		std::string impl = find_matching_impl_file(def_path, ctx);
		if (!impl.empty()) {
			if (!symbols_cache.contains(impl)) {
				std::vector<codemap_symbol_info> syms;
				fallback_find_symbols(impl, 1, syms);
				symbols_cache[impl] = std::move(syms);
			}
			const auto &impl_syms = symbols_cache[impl];
			const codemap_symbol_info *impl_found = find_symbol_by_hint(impl_syms, item.name);
			if (is_matching_function_symbol(impl_found, item.name)) {
				found = impl_found;
				def_path = impl;
			}
		}
	}

	if (!found) {
		return false;
	}

	int lsp_start = (def_pos_line > 0) ? def_pos_line : found->start_line;
	ref.target_file = def_path;
	ref.target_start_line = std::min(found->start_line, lsp_start);
	ref.target_end_line = std::max(found->end_line, ref.target_start_line);
	return true;
}

std::vector<outgoing_call_reference> call_resolver::extract_calls_from_slice(
	const std::string &safe_path,
	int start_line,
	int end_line,
	const std::vector<codemap_symbol_info> &doc_symbols,
	std::unordered_map<std::string, std::vector<codemap_symbol_info>> &symbols_cache,
	agentlib::tool_context *ctx,
	std::chrono::steady_clock::time_point deadline)
{
	std::ifstream in(safe_path);
	if (!in.is_open()) {
		return {};
	}

	std::vector<std::string> slice_lines;
	std::string current_line_text;
	int current_line_num = 1;
	while (std::getline(in, current_line_text)) {
		if (current_line_num >= start_line && current_line_num <= end_line) {
			slice_lines.push_back(current_line_text);
		}
		if (current_line_num > end_line) {
			break;
		}
		++current_line_num;
	}

	if (slice_lines.empty()) {
		return {};
	}

	auto candidates = call_token_extractor::extract_candidates(slice_lines, start_line);
	if (candidates.empty()) {
		return {};
	}

	std::unordered_set<std::string> defined_on_line_names;
	for (const auto &sym : doc_symbols) {
		defined_on_line_names.insert(std::format("{}:{}", sym.start_line, sym.name));
	}

	std::string norm_safe_path = fs_utils::make_relative_to_project(safe_path);
	std::vector<outgoing_call_reference> results;
	std::unordered_set<std::string> seen_names;

	for (const auto &cand : candidates) {
		if (std::chrono::steady_clock::now() >= deadline) {
			break;
		}
		if (seen_names.contains(cand.name)) {
			continue;
		}
		if (defined_on_line_names.contains(std::format("{}:{}", cand.line, cand.name))) {
			continue;
		}
		seen_names.insert(cand.name);

		auto raw_defs = project_manager::get_instance().lsp_query_definition(safe_path, cand.line - 1, cand.col);

		// Disambiguate multiple candidate definitions across files using caller context
		auto chosen = disambiguate_locations(raw_defs, safe_path, ctx);
		if (!chosen) {
			continue;
		}

		std::string norm_def = fs_utils::make_relative_to_project(chosen->path);
		if (norm_def.empty() || norm_def == norm_safe_path) {
			continue;
		}

		outgoing_call_reference ref;
		ref.caller_file = safe_path;
		ref.call_line = cand.line;
		ref.target_name = cand.name;
		ref.target_file = norm_def;
		int lsp_start = chosen->range.start_y + 1;
		int lsp_end = (chosen->range.end_y >= chosen->range.start_y) ? (chosen->range.end_y + 1) : lsp_start;
		ref.target_start_line = lsp_start;
		ref.target_end_line = lsp_end;
		ref.target_kind = "Function";
		ref.is_direct_read_call = true;

		// Attempt to look up or expand exact symbol bounds in symbols_cache
		if (!symbols_cache.contains(norm_def)) {
			std::vector<codemap_symbol_info> syms;
			fallback_find_symbols(norm_def, 1, syms);
			symbols_cache[norm_def] = std::move(syms);
		}
		const auto &syms = symbols_cache[norm_def];
		const auto *found = find_symbol_by_hint(syms, cand.name);
		if (found && is_matching_function_symbol(found, cand.name)) {
			ref.target_start_line = std::min(found->start_line, lsp_start);
			ref.target_end_line = std::max(found->end_line, lsp_end);
		}

		results.push_back(std::move(ref));
		if (results.size() >= 10) {
			break;
		}
	}

	return results;
}

} // namespace tools
