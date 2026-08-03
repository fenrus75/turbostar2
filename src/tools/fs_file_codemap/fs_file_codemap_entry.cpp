#include "fs_file_codemap.h"
#include "tools/codemap_utils.h"

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
	if (ctx.doc_provider && ctx.doc_provider->get_open_document(args_.safe_path)) {
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

	std::string table = format_codemap_table(args_.requested_path, symbols, /*rich_format=*/true, total_lines);

	// Check if this is a header file with a matching implementation file
	std::string matching_impl = find_matching_impl_file(args_.safe_path, ctx);
	if (!matching_impl.empty()) {
		auto impl_symbols = get_document_codemap_symbols(matching_impl, ctx, args_.min_lines);
		if (!impl_symbols.empty()) {
			std::filesystem::path ip(matching_impl);
			table += format_codemap_table(ip.filename().string(), impl_symbols, /*rich_format=*/true);
		}
	}

	return table;
}

} // namespace tools
