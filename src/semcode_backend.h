#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include "standard_lsp_backend.h"

/**
 * @brief Semcode-powered LSP and code intelligence backend.
 *
 * Implements a hybrid approach combining the semcode-lsp language server
 * (for fast JSON-RPC definition and reference navigation over stdio) with
 * the semcode CLI (for rich type exploration, struct definitions, call chains,
 * and workspace symbol queries).
 *
 * Automatically activated when a .semcode.db database exists in the project root
 * and the semcode-lsp binary is available.
 */
class semcode_backend : public standard_lsp_backend {
public:
	explicit semcode_backend(std::string project_root = "");
	~semcode_backend() override;

	[[nodiscard]] static bool is_available(const std::string &project_root);
	[[nodiscard]] static std::string find_semcode_lsp();
	[[nodiscard]] static std::string find_semcode_cli();

	// Document synchronization overrides: semcode uses working-directory overlay,
	// so avoid broadcasting full document bodies over stdio.
	void open_document(const std::string &filepath, const std::string &text) override;
	void update_document(const std::string &filepath, const std::string &text) override;

	// Interactive hover and navigation
	void request_hover(const std::string &filepath, int line, int character) override;
	void request_document_highlight(const std::string &filepath, int line, int character) override;
	void request_selection_range(const std::string &filepath, int line, int character) override;

	[[nodiscard]] bool is_supported_file(const std::string &filepath) const override;

	// Synchronous queries
	[[nodiscard]] std::vector<text_range> query_selection_ranges(const std::string &filepath, int line, int character) override;
	[[nodiscard]] std::vector<location_info> query_definition(const std::string &filepath, int line, int character) override;
	[[nodiscard]] std::vector<location_info> query_references(const std::string &filepath, int line, int character) override;
	[[nodiscard]] std::vector<symbol_info> query_workspace_symbols(const std::string &query) override;
	[[nodiscard]] std::vector<symbol_node> query_document_symbols(const std::string &filepath) override;
	[[nodiscard]] std::vector<call_hierarchy_item> query_call_hierarchy_outgoing(const std::string &filepath, int line, int character) override;
	[[nodiscard]] std::vector<outgoing_call_item> query_call_hierarchy_outgoing_batch(
		const std::string &filepath,
		const std::vector<std::pair<int, int>> &positions,
		std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) override;
	[[nodiscard]] std::vector<type_hierarchy_item> query_type_hierarchy_supertypes(const std::string &filepath, int line, int character) override;

protected:
	std::shared_ptr<server_instance> get_server_for_file(const std::string &filepath) override;

private:
	std::string project_root_;
	std::string semcode_lsp_path_;
	std::string semcode_cli_path_;

	[[nodiscard]] std::string run_semcode_query(const std::string &query) const;
	[[nodiscard]] static std::string extract_identifier_at(const std::string &filepath, int line, int character);
};
