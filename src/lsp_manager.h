#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "event_queue.h"
#include "lsp_backend.h"

/**
 * @brief High-level facade for Language Server Protocol and code intelligence backends.
 *
 * lsp_manager coordinates code intelligence queries across the editor, delegating
 * concrete LSP communications to an underlying lsp_backend strategy (such as
 * standard_lsp_backend or semcode_backend).
 */
class lsp_manager {
public:
	using location_info = lsp_backend::location_info;
	using symbol_info = lsp_backend::symbol_info;
	using symbol_node = lsp_backend::symbol_node;
	using call_hierarchy_item = lsp_backend::call_hierarchy_item;
	using outgoing_call_item = lsp_backend::outgoing_call_item;
	using type_hierarchy_item = lsp_backend::type_hierarchy_item;

	lsp_manager();
	explicit lsp_manager(std::unique_ptr<lsp_backend> backend);
	~lsp_manager();

	void set_backend(std::unique_ptr<lsp_backend> backend);
	[[nodiscard]] lsp_backend *get_backend() const noexcept;

	void start(event_queue &queue);
	void stop();

	void open_document(const std::string &filepath, const std::string &text);
	void update_document(const std::string &filepath, const std::string &text);
	void request_hover(const std::string &filepath, int line, int character);
	void request_document_highlight(const std::string &filepath, int line, int character);
	void request_selection_range(const std::string &filepath, int line, int character);
	[[nodiscard]] bool is_supported_file(const std::string &filepath) const;

	// Synchronous queries for tools
	[[nodiscard]] std::vector<text_range> query_selection_ranges(const std::string &filepath, int line, int character);
	[[nodiscard]] std::vector<location_info> query_definition(const std::string &filepath, int line, int character);
	[[nodiscard]] std::vector<location_info> query_references(const std::string &filepath, int line, int character);
	[[nodiscard]] std::vector<symbol_info> query_workspace_symbols(const std::string &query);
	[[nodiscard]] std::vector<symbol_node> query_document_symbols(const std::string &filepath);
	void invalidate_symbol_cache(const std::string &filepath);
	[[nodiscard]] std::vector<call_hierarchy_item> query_call_hierarchy_outgoing(const std::string &filepath, int line, int character);
	[[nodiscard]] std::vector<outgoing_call_item> query_call_hierarchy_outgoing_batch(
		const std::string &filepath,
		const std::vector<std::pair<int, int>> &positions,
		std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
	[[nodiscard]] std::vector<type_hierarchy_item> query_type_hierarchy_supertypes(const std::string &filepath, int line, int character);
	[[nodiscard]] std::optional<std::vector<diagnostic_info>> query_file_diagnostics(const std::string &filepath);
	void store_file_diagnostics(const std::string &filepath, const std::vector<diagnostic_info> &diags);

private:
	std::unique_ptr<lsp_backend> backend_;
};
