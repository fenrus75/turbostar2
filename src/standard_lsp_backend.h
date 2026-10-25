#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "lsp_backend.h"

namespace lsp
{
class Connection;
class MessageHandler;
class Process;
namespace io
{
class Stream;
}
} // namespace lsp

/*

# subclasses of standard_lsp_backend

| subclass        | filename                  |
| --------------- | ------------------------- |
| semcode_backend | src/semcode_backend.h     |

*/
/**
 * @brief Standard Language Server Protocol backend implementation.
 *
 * Launches and orchestrates background LSP server processes (such as clangd for C/C++
 * and pylsp for Python) using stdio JSON-RPC connections, and manages symbol caching,
 * document versions, and diagnostic updates.
 */
class standard_lsp_backend : public lsp_backend
{
      public:
	standard_lsp_backend();
	~standard_lsp_backend() override;

	void start(event_queue &queue) override;
	void stop() override;

	void open_document(const std::string &filepath, const std::string &text) override;
	void update_document(const std::string &filepath, const std::string &text) override;
	void save_document(const std::string &filepath, const std::string &text) override;
	void request_hover(const std::string &filepath, int line, int character) override;
	void request_document_highlight(const std::string &filepath, int line, int character) override;
	void request_selection_range(const std::string &filepath, int line, int character) override;
	[[nodiscard]] bool is_supported_file(const std::string &filepath) const override;

	[[nodiscard]] std::vector<text_range> query_selection_ranges(const std::string &filepath, int line, int character) override;
	[[nodiscard]] std::vector<location_info> query_definition(const std::string &filepath, int line, int character) override;
	[[nodiscard]] std::vector<location_info> query_type_definition(const std::string &filepath, int line, int character) override;
	[[nodiscard]] std::vector<location_info> query_references(const std::string &filepath, int line, int character) override;
	[[nodiscard]] std::vector<symbol_info> query_workspace_symbols(const std::string &query) override;
	[[nodiscard]] virtual std::vector<symbol_node> query_document_symbols(const std::string &filepath) override;
	void prefetch_document_symbols(const std::string &filepath) override;
	void invalidate_symbol_cache(const std::string &filepath) override;
	[[nodiscard]] std::vector<call_hierarchy_item> query_call_hierarchy_outgoing(const std::string &filepath, int line,
										     int character) override;
	[[nodiscard]] std::vector<outgoing_call_item> query_call_hierarchy_outgoing_batch(
	    const std::string &filepath, const std::vector<std::pair<int, int>> &positions,
	    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) override;
	[[nodiscard]] std::vector<type_hierarchy_item> query_type_hierarchy_supertypes(const std::string &filepath, int line,
										       int character) override;
	[[nodiscard]] std::optional<std::vector<diagnostic_info>> query_file_diagnostics(const std::string &filepath) override;
	void store_file_diagnostics(const std::string &filepath, const std::vector<diagnostic_info> &diags) override;
	void flush_doc_queue_for_testing();

      protected:
	struct server_instance {
		std::string language_id;
		std::unique_ptr<lsp::Process> process;
		std::unique_ptr<lsp::Connection> connection;
		std::unique_ptr<lsp::MessageHandler> message_handler;
		std::thread message_thread;
		std::atomic<bool> is_running{false};
	};

	virtual void start_server(const std::string &name, const std::vector<std::string> &args, const std::string &language_id);
	virtual std::shared_ptr<server_instance> get_server_for_file(const std::string &filepath, bool start_if_needed = true);

	enum class doc_action_type {
		open,
		update,
		save
	};

	struct doc_action {
		doc_action_type type;
		std::string filepath;
		std::string text;
	};

	/*
	 * doc_queue_mutex_ protects doc_queue_ of pending asynchronous LSP document synchronization tasks.
	 * Locking Rules:
	 * - Held briefly when enqueuing actions from open_document(), update_document(), save_document()
	 *   or dequeuing in the background doc_worker_thread.
	 */
	std::mutex doc_queue_mutex_;
	std::condition_variable doc_queue_cv_;
	std::queue<doc_action> doc_queue_;
	std::thread doc_worker_thread_;
	std::atomic<bool> doc_worker_running_{false};

	void ensure_worker_started();
	void stop_worker();
	void process_doc_action(const doc_action &action);
	void process_open_document(const std::string &filepath, const std::string &text);
	void process_update_document(const std::string &filepath, const std::string &text);
	void process_save_document(const std::string &filepath, const std::string &text);

	std::vector<std::shared_ptr<server_instance>> servers_;

	/*
	 * servers_mutex_ protects the servers_ active LSP server instances list.
	 * Locking Rules:
	 * - Held briefly when starting, stopping, retrieving, or checking the status
	 *   of LSP servers.
	 */
	std::mutex servers_mutex_;
	std::atomic<event_queue *> global_queue_{nullptr};

	/*
	 * doc_mutex_ protects the doc_versions_ map of document URIs to document versions.
	 * Locking Rules:
	 * - Held briefly when opening or updating documents to increment their version sequence.
	 */
	std::mutex doc_mutex_;
	std::unordered_map<std::string, int> doc_versions_;

	struct symbol_cache_entry {
		std::filesystem::file_time_type last_mtime;
		std::chrono::steady_clock::time_point last_fetch_time{std::chrono::steady_clock::now()};
		std::vector<symbol_node> symbols;
	};

	/*
	 * symbol_cache_mutex_ protects symbol_cache_ map of filepaths to cached symbol_node ASTs,
	 * as well as in_flight_symbol_queries_ set tracking in-flight asynchronous document symbol requests.
	 * Locking Rules:
	 * - Held briefly when reading or writing symbol cache entries in query_document_symbols(),
	 *   prefetch_document_symbols(), and invalidate_symbol_cache().
	 */
	mutable std::mutex symbol_cache_mutex_;
	std::unordered_map<std::string, symbol_cache_entry> symbol_cache_;
	std::unordered_set<std::string> in_flight_symbol_queries_;

	/*
	 * diagnostics_mutex_ protects file_diagnostics_ map of filepaths to active LSP diagnostic lists.
	 * Locking Rules:
	 * - Held briefly when storing incoming PublishDiagnostics notifications or querying diagnostics
	 *   in store_file_diagnostics() and query_file_diagnostics().
	 */
	mutable std::mutex diagnostics_mutex_;
	std::unordered_map<std::string, std::vector<diagnostic_info>> file_diagnostics_;

	struct hover_cache_entry {
		std::string payload;
		std::chrono::steady_clock::time_point cached_at{std::chrono::steady_clock::now()};
	};

	/*
	 * hover_cache_mutex_ protects hover_cache_ map of hover keys (e.g. file:line:col) to cached payloads.
	 * Locking Rules:
	 * - Held briefly when reading, writing, or invalidating hover cache entries across
	 *   the UI thread and background LSP reader threads or worker loops.
	 */
	mutable std::mutex hover_cache_mutex_;
	std::unordered_map<std::string, hover_cache_entry> hover_cache_;

	[[nodiscard]] std::optional<std::string> get_cached_hover(const std::string &key) const;
	void set_cached_hover(const std::string &key, std::string payload);
	void invalidate_hover_cache(const std::string &filepath);
};
