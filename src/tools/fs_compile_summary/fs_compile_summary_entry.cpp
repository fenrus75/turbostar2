#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include "../../build_error_manager.h"
#include "../../fs_utils.h"
#include "fs_compile_summary.h"

namespace tools
{

bool fs_compile_summary_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string fs_compile_summary_tool::execute(agentlib::tool_context &ctx)
{
	struct file_stats {
		int compiler_errors = 0;
		int compiler_warnings = 0;
		int lsp_errors = 0;
		int lsp_warnings = 0;
	};

	std::map<std::string, file_stats> workspace_stats;
	std::set<std::string> unique_paths_to_check;

	// 1. Gather all file paths that have build errors
	const auto &build_errors = build_error_manager::get_instance().get_errors();
	for (const auto &err : build_errors) {
		if (!err.filepath.empty()) {
			unique_paths_to_check.insert(err.filepath);
		}
	}

	// 2. Gather all open document paths (might have LSP diagnostics)
	if (ctx.doc_provider) {
		auto open_docs = ctx.doc_provider->get_open_document_paths();
		for (const auto &doc_path : open_docs) {
			unique_paths_to_check.insert(doc_path);
		}
	}

	// 3. Process each path securely
	std::filesystem::path cwd = ctx.fs_security.get_working_directory();

	for (const auto &raw_path : unique_paths_to_check) {
		std::string safe_path;
		std::string error;

		// Ensure the agent is actually allowed to see this file
		if (!ctx.fs_security.validate_access(raw_path, agentlib::access_type::read, safe_path, error)) {
			continue;
		}

		std::filesystem::path rel_path = std::filesystem::relative(safe_path, cwd);
		std::string rel_str = rel_path.string();
		if (rel_str.empty() || rel_str == ".")
			rel_str = safe_path; // Fallback to absolute if something weird happens

		file_stats stats;
		bool has_issues = false;

		// Tally Compiler Diagnostics
		for (const auto &err : build_errors) {
			// Check if this error belongs to this safe_path
			std::string err_safe;
			std::string err_error;
			if (ctx.fs_security.validate_access(err.filepath, agentlib::access_type::read, err_safe, err_error) &&
			    err_safe == safe_path) {
				if (err.is_warning)
					stats.compiler_warnings++;
				else
					stats.compiler_errors++;
				has_issues = true;
			}
		}

		// Tally LSP Diagnostics
		if (ctx.doc_provider) {
			auto doc_snapshot = ctx.doc_provider->get_open_document(safe_path);
			if (doc_snapshot) {
				auto diagnostics = doc_snapshot->get_diagnostics();
				for (const auto &d : diagnostics) {
					if (d.severity == "Error")
						stats.lsp_errors++;
					else if (d.severity == "Warning")
						stats.lsp_warnings++;
					else if (d.severity == "Information" || d.severity == "Hint") {
						// Optional: Could add these as columns if needed, but errors/warnings are most important
					}
					if (d.severity == "Error" || d.severity == "Warning") {
						has_issues = true;
					}
				}
			}
		}

		if (has_issues) {
			workspace_stats[rel_str] = stats;
		}
	}

	if (workspace_stats.empty()) {
		return "No compilation errors or warnings found.";
	}

	std::stringstream ss;
	ss << "# Workspace Compilation Summary\n\n";
	ss << "| File | Compiler Errors | Compiler Warnings | LSP Errors | LSP Warnings |\n";
	ss << "| ---- | --------------- | ----------------- | ---------- | ------------ |\n";

	for (const auto &[file, stats] : workspace_stats) {
		ss << "| `" << file << "` | " << stats.compiler_errors << " | " << stats.compiler_warnings << " | " << stats.lsp_errors
		   << " | " << stats.lsp_warnings << " |\n";
	}

	return ss.str();
}

} // namespace tools
