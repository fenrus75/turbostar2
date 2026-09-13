#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "semcode_indexer.h"
#include "standard_lsp_backend.h"

/**
 * @brief Semcode-powered LSP and code intelligence backend.
 *
 * Implements a hybrid approach combining the semcode-lsp language server
 * (for fast JSON-RPC definition and reference navigation over stdio) with
 * a high-performance local SQLite indexer (for instant type exploration, struct definitions,
 * call chains, and workspace symbol queries).
 *
 * Automatically activated when a .semcode.db database exists in the project root
 * and the semcode-lsp binary is available.
 */
class semcode_backend : public standard_lsp_backend
{
      public:
	explicit semcode_backend(std::string project_root = "");
	~semcode_backend() override;

	void start(event_queue &queue) override;
	void stop() override;

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
	[[nodiscard]] std::vector<location_info> query_type_definition(const std::string &filepath, int line, int character) override;
	[[nodiscard]] std::vector<location_info> query_references(const std::string &filepath, int line, int character) override;
	[[nodiscard]] std::vector<symbol_info> query_workspace_symbols(const std::string &query) override;
	[[nodiscard]] std::vector<symbol_node> query_document_symbols(const std::string &filepath) override;
	[[nodiscard]] std::vector<call_hierarchy_item> query_call_hierarchy_outgoing(const std::string &filepath, int line,
										     int character) override
	{
		return query_call_hierarchy_outgoing(filepath, line, character, std::chrono::steady_clock::time_point::max());
	}
	[[nodiscard]] std::vector<call_hierarchy_item> query_call_hierarchy_outgoing(const std::string &filepath, int line, int character,
										     std::chrono::steady_clock::time_point deadline);
	[[nodiscard]] std::vector<outgoing_call_item> query_call_hierarchy_outgoing_batch(
	    const std::string &filepath, const std::vector<std::pair<int, int>> &positions,
	    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) override;
	[[nodiscard]] std::vector<type_hierarchy_item> query_type_hierarchy_supertypes(const std::string &filepath, int line,
										       int character) override;
	[[nodiscard]] static std::string extract_identifier_at(const std::string &filepath, int line, int character);

	[[nodiscard]] semcode_indexer *get_indexer() const noexcept;
	void set_indexer(std::unique_ptr<semcode_indexer> indexer);

	void init_indexer();

      protected:
	std::shared_ptr<server_instance> get_server_for_file(const std::string &filepath) override;

      private:
	std::string project_root_;
	std::string semcode_lsp_path_;
	std::string semcode_cli_path_;

	struct hover_request {
		std::string filepath;
		int line{0};
		int character{0};
		uint64_t request_id{0};
	};

	std::thread hover_thread_;
	std::atomic<bool> hover_stopping_{false};
	std::atomic<uint64_t> hover_counter_{0};

	/**
	 * @brief Mutex protecting the asynchronous hover request queue.
	 *
	 * (1) Protects `pending_hover_request_` against concurrent access between the editor/UI
	 * thread dispatching `request_hover` and the background `hover_worker_loop` executing
	 * SQLite queries.
	 * (2) Locking rules: Short, bounded critical section. Never call shell execution,
	 * disk I/O, or acquire any other lock while holding `hover_mutex_`.
	 */
	std::mutex hover_mutex_;
	std::condition_variable hover_cv_;
	std::optional<hover_request> pending_hover_request_;

	void hover_worker_loop();

	/**
	 * @brief Mutex protecting access to the semcode_indexer instance during initialization and querying.
	 *
	 * (1) Protects `indexer_` against concurrent access between the startup/indexing threads
	 * and concurrent query operations.
	 * (2) Locking rules: Short, non-reentrant critical section. Never held during subprocess launches.
	 */
	mutable std::mutex indexer_mutex_;
	std::unique_ptr<semcode_indexer> indexer_;
};
