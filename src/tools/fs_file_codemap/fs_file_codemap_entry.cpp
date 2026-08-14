#include "fs_file_codemap.h"
#include "codemap_utils.h"

#include <fstream>

namespace tools {

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

	size_t total_symbols_count = symbols.size();
	size_t omitted_count = 0;

	if (args_.max_symbols > 0 && symbols.size() > static_cast<size_t>(args_.max_symbols)) {
		omitted_count = symbols.size() - args_.max_symbols;
		symbols.resize(args_.max_symbols);
	}

	std::string table = format_codemap_table(args_.requested_path, symbols, /*rich_format=*/true, total_lines, total_symbols_count, omitted_count, &ctx);

	// Check if this is a header file with a matching implementation file
	std::string matching_impl = find_matching_impl_file(args_.safe_path, ctx);
	if (!matching_impl.empty()) {
		auto impl_symbols = get_document_codemap_symbols(matching_impl, ctx, args_.min_lines);
		if (!impl_symbols.empty()) {
			size_t impl_total = impl_symbols.size();
			size_t impl_omitted = 0;
			if (args_.max_symbols > 0 && impl_symbols.size() > static_cast<size_t>(args_.max_symbols)) {
				impl_omitted = impl_symbols.size() - args_.max_symbols;
				impl_symbols.resize(args_.max_symbols);
			}
			std::filesystem::path ip(matching_impl);
			table += format_codemap_table(ip.filename().string(), impl_symbols, /*rich_format=*/true, 0, impl_total, impl_omitted, &ctx);
		}
	}

	return table;
}

} // namespace tools
