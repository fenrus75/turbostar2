#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "event_queue.h"

/*

# subclasses of lsp_backend

| subclass             | filename                  |
| -------------------- | ------------------------- |
| standard_lsp_backend | src/standard_lsp_backend.h|
| semcode_backend      | src/semcode_backend.h     |

*/
/**
 * @brief Abstract base class defining the interface for LSP and code intelligence backends.
 *
 * Provides virtual interfaces for document synchronization, interactive hover/highlight/selection,
 * symbol indexing, call and type hierarchies, and diagnostics. Subclasses implement these operations
 * using specific server protocols or hybrid tool combinations (e.g., clangd/pylsp or semcode).
 */
class lsp_backend
{
      public:
	virtual ~lsp_backend();

	struct location_info {
		std::string path;
		text_range range;
		std::string kind;
		std::string underlying_type;

		location_info() = default;
		location_info(std::string p, text_range r, std::string k = "", std::string ut = "")
		    : path(std::move(p)), range(r), kind(std::move(k)), underlying_type(std::move(ut))
		{
		}
	};

	struct symbol_info {
		std::string name;
		int kind;
		location_info location;
	};

	struct symbol_node {
		std::string name;
		int kind;
		text_range range;
		text_range selection_range;
		std::vector<symbol_node> children;
	};

	struct call_hierarchy_item {
		std::string name;
		int kind;
		std::string detail;
		std::string uri;
		text_range range;
		text_range selection_range;
	};

	struct outgoing_call_item {
		int call_line;
		call_hierarchy_item item;
	};

	struct type_hierarchy_item {
		std::string name;
		int kind;
		std::string detail;
		std::string uri;
		text_range range;
		text_range selection_range;
	};

	virtual void start(event_queue &queue) = 0;
	virtual void stop() = 0;

	virtual void open_document(const std::string &filepath, const std::string &text) = 0;
	virtual void update_document(const std::string &filepath, const std::string &text) = 0;
	virtual void request_hover(const std::string &filepath, int line, int character) = 0;
	virtual void request_document_highlight(const std::string &filepath, int line, int character) = 0;
	virtual void request_selection_range(const std::string &filepath, int line, int character) = 0;
	[[nodiscard]] virtual bool is_supported_file(const std::string &filepath) const = 0;

	// Synchronous queries for tools and editor features
	[[nodiscard]] virtual std::vector<text_range> query_selection_ranges(const std::string &filepath, int line, int character) = 0;
	[[nodiscard]] virtual std::vector<location_info> query_definition(const std::string &filepath, int line, int character) = 0;
	[[nodiscard]] virtual std::vector<location_info> query_type_definition(const std::string &filepath, int line, int character) = 0;
	[[nodiscard]] virtual std::vector<location_info> query_references(const std::string &filepath, int line, int character) = 0;
	[[nodiscard]] virtual std::vector<symbol_info> query_workspace_symbols(const std::string &query) = 0;
	[[nodiscard]] virtual std::vector<symbol_node> query_document_symbols(const std::string &filepath) = 0;
	virtual void invalidate_symbol_cache(const std::string &filepath) = 0;
	[[nodiscard]] virtual std::vector<call_hierarchy_item> query_call_hierarchy_outgoing(const std::string &filepath, int line,
											     int character) = 0;
	[[nodiscard]] virtual std::vector<outgoing_call_item> query_call_hierarchy_outgoing_batch(
	    const std::string &filepath, const std::vector<std::pair<int, int>> &positions,
	    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) = 0;
	[[nodiscard]] virtual std::vector<type_hierarchy_item> query_type_hierarchy_supertypes(const std::string &filepath, int line,
											       int character) = 0;
	[[nodiscard]] virtual std::optional<std::vector<diagnostic_info>> query_file_diagnostics(const std::string &filepath) = 0;
	virtual void store_file_diagnostics(const std::string &filepath, const std::vector<diagnostic_info> &diags) = 0;
};
