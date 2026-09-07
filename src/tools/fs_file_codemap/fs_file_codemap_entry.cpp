#include "fs_file_codemap.h"
#include "codemap_utils.h"
#include "fs_utils.h"

#include <fstream>
#include <iostream>
#include <format>

namespace tools {

namespace {

bool is_structural_symbol(const codemap_symbol_info &sym)
{
	return sym.kind_str == "Function" ||
	       sym.kind_str == "Method" ||
	       sym.kind_str == "Class" ||
	       sym.kind_str == "Struct" ||
	       sym.kind_str == "Class/Struct" ||
	       sym.kind_str == "Interface" ||
	       sym.kind_str == "Heading";
}

} // namespace

fs_file_codemap_tool::fs_file_codemap_tool(fs_file_codemap_args args) : args_(std::move(args))
{
}

std::shared_ptr<agentlib::agent_interaction> fs_file_codemap_tool::get_interaction() const
{
	return interaction_;
}

bool fs_file_codemap_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &out_error) const
{
	if (args_.safe_path.empty()) {
		out_error = "Error: File path is empty.";
		return false;
	}
	return true;
}

std::string fs_file_codemap_tool::execute(agentlib::tool_context &ctx)
{
	// Erase drift tracker for this path on fresh codemap query
	ctx.file_drift_tracker.erase(args_.safe_path);

	size_t total_lines = 0;
	if (args_.safe_path.find("://") != std::string::npos) {
		auto vfs = ctx.fs_security.get_vfs();
		if (vfs) {
			auto view_opt = vfs->read_file(args_.safe_path);
			if (view_opt && *view_opt) {
				std::string_view view = (*view_opt)->view();
				total_lines = std::count(view.begin(), view.end(), '\n');
				if (!view.empty() && view.back() != '\n') {
					total_lines++;
				}
			}
		}
	} else if (ctx.doc_provider && ctx.doc_provider->get_open_document(args_.safe_path)) {
		total_lines = ctx.doc_provider->get_open_document(args_.safe_path)->get_line_count();
	} else {
		std::ifstream file(args_.safe_path);
		std::string line;
		while (std::getline(file, line)) {
			total_lines++;
		}
	}

	auto symbols = get_document_codemap_symbols(args_.safe_path, ctx, args_.min_lines);
	if (symbols.empty()) {
		return "No functions, classes, or symbols found in " + args_.requested_path + ".";
	}

	size_t raw_symbols_count = symbols.size();
	size_t pruned_count = 0;

	// Kind-aware adaptive pruning:
	// If caller did not explicitly pass min_lines and the symbol count > 50, prune
	// non-structural 1-line symbols (e.g. enum members, fields, variables) while
	// preserving 1-line function/method prototypes, classes, structs, etc.
	if (!args_.min_lines_explicit && symbols.size() > 50) {
		std::vector<codemap_symbol_info> filtered;
		filtered.reserve(symbols.size());
		for (const auto &sym : symbols) {
			if (sym.line_count <= 1 && !is_structural_symbol(sym)) {
				pruned_count++;
			} else {
				filtered.push_back(sym);
			}
		}
		if (pruned_count > 0) {
			symbols = std::move(filtered);
		}
	}

	size_t total_symbols_count = symbols.size();
	size_t omitted_count = 0;

	if (args_.max_symbols > 0 && symbols.size() > static_cast<size_t>(args_.max_symbols)) {
		omitted_count = symbols.size() - args_.max_symbols;
		symbols.resize(args_.max_symbols);
	}

	// Record reported symbol timestamps in codemap history
	auto now = std::chrono::steady_clock::now();
	auto &history = ctx.codemap_history[args_.safe_path];
	for (const auto &sym : symbols) {
		history.reported_symbols[sym.name] = now;
	}

	std::string table = format_codemap_table(args_.requested_path, symbols, total_lines, total_symbols_count, omitted_count, &ctx, args_.full, pruned_count, raw_symbols_count);
	if (pruned_count > 0) {
		table += std::format("*Note: {} total symbols found. Pruned {} 1-line fields/enum members. Pass min_lines=1 to view all symbols or min_lines=2/3 to filter getters.*\n\n", raw_symbols_count, pruned_count);
	}

	// Check if this is a header file with a matching implementation file
	std::string matching_impl = find_matching_impl_file(args_.safe_path, ctx);
	if (!matching_impl.empty()) {
		std::string safe_impl;
		std::string out_err;
		if (ctx.fs_security.validate_access(matching_impl, agentlib::access_type::read, safe_impl, out_err) && fs_utils::is_regular_file(safe_impl)) {
			auto impl_symbols = get_document_codemap_symbols(safe_impl, ctx, args_.min_lines);
			if (!impl_symbols.empty()) {
				size_t raw_impl_count = impl_symbols.size();
				size_t impl_pruned_count = 0;

				if (!args_.min_lines_explicit && impl_symbols.size() > 50) {
					std::vector<codemap_symbol_info> filtered;
					filtered.reserve(impl_symbols.size());
					for (const auto &sym : impl_symbols) {
						if (sym.line_count <= 1 && !is_structural_symbol(sym)) {
							impl_pruned_count++;
						} else {
							filtered.push_back(sym);
						}
					}
					if (impl_pruned_count > 0) {
						impl_symbols = std::move(filtered);
					}
				}

				size_t impl_total = impl_symbols.size();
				size_t impl_omitted = 0;
				if (args_.max_symbols > 0 && impl_symbols.size() > static_cast<size_t>(args_.max_symbols)) {
					impl_omitted = impl_symbols.size() - args_.max_symbols;
					impl_symbols.resize(args_.max_symbols);
				}
				std::filesystem::path ip(safe_impl);
				auto &impl_history = ctx.codemap_history[safe_impl];
				for (const auto &sym : impl_symbols) {
					impl_history.reported_symbols[sym.name] = now;
				}
				table += format_codemap_table(ip.filename().string(), impl_symbols, 0, impl_total, impl_omitted, &ctx, args_.full, impl_pruned_count, raw_impl_count);
				if (impl_pruned_count > 0) {
					table += std::format("*Note: {} total symbols found. Pruned {} 1-line fields/enum members. Pass min_lines=1 to view all symbols or min_lines=2/3 to filter getters.*\n\n", raw_impl_count, impl_pruned_count);
				}
			}
		}
	}

	return fs_utils::wrap_prompt_untrusted_data_tag("codemap_result", table);
}

} // namespace tools
