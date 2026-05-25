#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <vector>
#include "event_queue.h"

namespace lsp {
	class Connection;
	class MessageHandler;
	class Process;
	namespace io {
		class Stream;
	}
}

class lsp_manager
{
      public:
	lsp_manager();
	~lsp_manager();

	void start(event_queue &queue);
	void stop();

	void open_document(const std::string &filepath, const std::string &text);
	void update_document(const std::string &filepath, const std::string &text);
	void request_hover(const std::string &filepath, int line, int character);
	void request_document_highlight(const std::string &filepath, int line, int character);
	void request_selection_range(const std::string &filepath, int line, int character);
	bool is_supported_file(const std::string &filepath) const;

	// Synchronous queries for tools
	std::vector<text_range> query_selection_ranges(const std::string &filepath, int line, int character);

	struct location_info {
		std::string path;
		text_range range;
	};
	std::vector<location_info> query_definition(const std::string &filepath, int line, int character);
	std::vector<location_info> query_references(const std::string &filepath, int line, int character);

	struct symbol_info {
		std::string name;
		int kind;
		location_info location;
	};
	std::vector<symbol_info> query_workspace_symbols(const std::string &query);

	struct call_hierarchy_item {
		std::string name;
		int kind;
		std::string detail;
		std::string uri;
		text_range range;
		text_range selection_range;
	};
	std::vector<call_hierarchy_item> query_call_hierarchy_outgoing(const std::string &filepath, int line, int character);

      private:
	struct server_instance {
		std::string language_id;
		std::unique_ptr<lsp::Process> process;
		std::unique_ptr<lsp::Connection> connection;
		std::unique_ptr<lsp::MessageHandler> message_handler;
		std::thread message_thread;
		std::atomic<bool> is_running{false};
	};

	void start_server(const std::string& name, const std::vector<std::string>& args, const std::string& language_id);
	server_instance* get_server_for_file(const std::string& filepath);

	std::vector<std::unique_ptr<server_instance>> servers_;
	std::mutex servers_mutex_;
	event_queue *global_queue_{nullptr};
	
	std::mutex doc_mutex_;
	std::unordered_map<std::string, int> doc_versions_;
};
