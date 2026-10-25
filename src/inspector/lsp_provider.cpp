#include "inspector/lsp_provider.h"
#include <format>
#include <thread>
#include "call_resolver.h"
#include "call_synthesizer.h"
#include "document.h"
#include "inspector/inspector_helpers.h"

lsp_inspector_provider::lsp_inspector_provider(std::function<document *()> active_doc_fn)
    : active_doc_fn_(std::move(active_doc_fn))
{
}

bool lsp_inspector_provider::has_actual_project_path(std::string_view loc)
{
	while (!loc.empty() && (loc.front() == ' ' || loc.front() == '\t')) {
		loc.remove_prefix(1);
	}
	while (!loc.empty() && (loc.back() == ' ' || loc.back() == '\t')) {
		loc.remove_suffix(1);
	}
	if (loc.empty() || loc.starts_with("<")) {
		return false;
	}
	if (loc.starts_with("/usr/") || loc.starts_with("/opt/") ||
	    loc.starts_with("file:///usr/") || loc.starts_with("file:///opt/")) {
		return false;
	}
	if (loc.find("/usr/include/") != std::string_view::npos) {
		return false;
	}
	// An actual path must contain a directory separator
	return loc.find('/') != std::string_view::npos || loc.find('\\') != std::string_view::npos;
}

bool lsp_inspector_provider::is_system_declaration(std::string_view loc)
{
	return !has_actual_project_path(loc);
}

static std::string format_context_markdown(const turbostar::context_menu_data &ctx)
{
	inspector::markdown_builder mb;
	std::string disp_sig = ctx.type_synopsis.empty() ? ctx.token : ctx.type_synopsis;
	mb.header("Signature:").code(disp_sig).newline();

	if (!ctx.suggestions.empty()) {
		mb.header("Args:");
		for (size_t i = 0; i < ctx.suggestions.size() && i < 3; ++i) {
			if (i > 0) {
				mb.text("  ");
			}
			mb.numbered_arg(static_cast<int>(i + 1), ctx.suggestions[i].call_text);
		}
		mb.newline();
	}

	if (!ctx.declaration_location.empty()) {
		mb.header("Decl:").text(ctx.declaration_location).newline();
	}
	return mb.build();
}

inspector_result lsp_inspector_provider::query(const inspector_query &q)
{
	inspector_result res;
	res.provider_name = "lsp";

	if (q.token.empty()) {
		res.score = inspector_scores::INVALID;
		res.markdown = "*[No symbol under cursor]*";
		res.is_final = true;
		return res;
	}

	document *doc = active_doc_fn_ ? active_doc_fn_() : nullptr;
	if (doc) {
		auto ctx = turbostar::call_synthesizer::analyze_context_at(*doc, q.line, q.col, false);
		if (ctx.is_resolved) {
			res.score = has_actual_project_path(ctx.declaration_location) ?
			                inspector_scores::LSP_RESOLVED :
			                inspector_scores::LSP_SYSTEM_HEADER;
			res.markdown = format_context_markdown(ctx);
			res.is_final = true;
			return res;
		}
	}

	// Unresolved locally: provisional resolving state
	res.score = inspector_scores::RESOLVING_HINT;
	res.markdown = std::format("`{}` *[resolving...]*", q.token);
	res.is_final = false;
	return res;
}

void lsp_inspector_provider::query_async(const inspector_query &q,
					 std::function<void(inspector_result)> callback)
{
	std::string doc_path = q.file_path;
	int req_line = q.line;
	int req_col = q.col;
	std::string req_token = q.token;

	std::thread([doc_path, req_line, req_col, req_token, callback]() {
		auto sig = tools::resolve_function_signature(doc_path, req_line, req_col, req_token);

		inspector_result res;
		res.provider_name = "lsp";
		res.is_final = true;

		if (sig) {
			res.score = has_actual_project_path(sig->declaration_location) ?
			                inspector_scores::LSP_RESOLVED :
			                inspector_scores::LSP_SYSTEM_HEADER;
			inspector::markdown_builder mb;
			mb.header("Signature:").code(sig->raw_signature).newline();

			auto sig_info = turbostar::call_synthesizer::parse_function_signature(sig->raw_signature);
			if (sig_info && !sig_info->parameters.empty()) {
				mb.header("Args:");
				for (size_t i = 0; i < sig_info->parameters.size() && i < 3; ++i) {
					if (i > 0) {
						mb.text("  ");
					}
					mb.numbered_arg(static_cast<int>(i + 1), sig_info->parameters[i].name);
				}
				mb.newline();
			}
			if (!sig->declaration_location.empty()) {
				mb.header("Decl:").text(sig->declaration_location).newline();
			}
			res.markdown = mb.build();
		} else {
			res.score = inspector_scores::LSP_FALLBACK_TOKEN;
			res.markdown = std::format("`{}`", req_token);
		}

		if (callback) {
			callback(res);
		}
	}).detach();
}
