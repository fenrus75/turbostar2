#include "ui/inspector_provider.h"
#include <algorithm>
#include <format>
#include <thread>
#include "call_resolver.h"
#include "call_synthesizer.h"
#include "document.h"

// ============================================================================
// fallback_inspector_provider
// ============================================================================

inspector_result fallback_inspector_provider::query(const inspector_query &q)
{
	inspector_result res;
	res.provider_name = "token";
	res.is_final = true;

	if (q.token.empty()) {
		res.score = 0;
		res.markdown = "*[No symbol under cursor]*";
	} else {
		res.score = 15;
		res.markdown = std::format("`{}`", q.token);
	}
	return res;
}

// ============================================================================
// lsp_inspector_provider
// ============================================================================

lsp_inspector_provider::lsp_inspector_provider(std::function<document *()> active_doc_fn)
    : active_doc_fn_(std::move(active_doc_fn))
{
}

static std::string format_context_markdown(const turbostar::context_menu_data &ctx)
{
	std::string md;
	std::string disp_sig = ctx.type_synopsis.empty() ? ctx.token : ctx.type_synopsis;
	md += std::format("**Signature:** `{}`\n", disp_sig);

	if (!ctx.suggestions.empty()) {
		md += "**Args:** ";
		for (size_t i = 0; i < ctx.suggestions.size() && i < 3; ++i) {
			if (i > 0) {
				md += "  ";
			}
			md += std::format("{}. `{}`", i + 1, ctx.suggestions[i].call_text);
		}
		md += "\n";
	}

	if (!ctx.declaration_location.empty()) {
		md += std::format("**Decl:** `{}`\n", ctx.declaration_location);
	}
	return md;
}

inspector_result lsp_inspector_provider::query(const inspector_query &q)
{
	inspector_result res;
	res.provider_name = "lsp";

	if (q.token.empty()) {
		res.score = 0;
		res.markdown = "*[No symbol under cursor]*";
		res.is_final = true;
		return res;
	}

	document *doc = active_doc_fn_ ? active_doc_fn_() : nullptr;
	if (doc) {
		auto ctx = turbostar::call_synthesizer::analyze_context_at(*doc, q.line, q.col, false);
		if (ctx.is_resolved) {
			res.score = 90;
			res.markdown = format_context_markdown(ctx);
			res.is_final = true;
			return res;
		}
	}

	// Unresolved locally: provisional resolving state
	res.score = 10;
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
			res.score = 90;
			std::string md = std::format("**Signature:** `{}`\n", sig->raw_signature);
			auto sig_info = turbostar::call_synthesizer::parse_function_signature(sig->raw_signature);
			if (sig_info && !sig_info->parameters.empty()) {
				md += "**Args:** ";
				for (size_t i = 0; i < sig_info->parameters.size() && i < 3; ++i) {
					if (i > 0) {
						md += "  ";
					}
					md += std::format("{}. `{}`", i + 1, sig_info->parameters[i].name);
				}
				md += "\n";
			}
			if (!sig->declaration_location.empty()) {
				md += std::format("**Decl:** `{}`\n", sig->declaration_location);
			}
			res.markdown = std::move(md);
		} else {
			res.score = 20;
			res.markdown = std::format("`{}`", req_token);
		}

		if (callback) {
			callback(res);
		}
	}).detach();
}

// ============================================================================
// inspector_coordinator
// ============================================================================

void inspector_coordinator::register_provider(std::unique_ptr<inspector_provider> provider)
{
	std::lock_guard<std::mutex> lock(mutex_);
	providers_.push_back(std::move(provider));
}

inspector_result inspector_coordinator::query(const inspector_query &q,
					      std::function<void(inspector_result)> async_callback)
{
	std::vector<inspector_provider *> prov_ptrs;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (const auto &p : providers_) {
			prov_ptrs.push_back(p.get());
		}
	}

	inspector_result highest_result;
	highest_result.score = -1;

	for (auto *p : prov_ptrs) {
		auto res = p->query(q);
		if (res.score > highest_result.score) {
			highest_result = res;
		}

		if (!res.is_final && async_callback) {
			p->query_async(q, [async_callback](inspector_result async_res) {
				async_callback(async_res);
			});
		}
	}

	if (highest_result.score < 0) {
		highest_result.score = 0;
		highest_result.provider_name = "none";
		highest_result.markdown = "*[No symbol under cursor]*";
		highest_result.is_final = true;
	}

	return highest_result;
}
