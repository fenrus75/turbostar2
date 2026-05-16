#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include "event_queue.h"

namespace lsp {
	class Connection;
	class MessageHandler;
	class Process;
	namespace io {
		class Stream;
	}
}

class clangd_manager
{
      public:
	static clangd_manager &get_instance();

	void start(event_queue &queue);
	void stop();

	void open_document(const std::string &filepath, const std::string &text);
	void update_document(const std::string &filepath, const std::string &text);
	void request_hover(const std::string &filepath, int line, int character);
	void request_document_highlight(const std::string &filepath, int line, int character);

      private:
	clangd_manager() = default;
	~clangd_manager();

	void message_loop();

	std::unique_ptr<lsp::Process> process_;
	std::unique_ptr<lsp::Connection> connection_;
	std::unique_ptr<lsp::MessageHandler> message_handler_;
	
	std::thread message_thread_;
	std::atomic<bool> is_running_{false};
	event_queue *global_queue_{nullptr};
	
	std::mutex doc_mutex_;
	std::unordered_map<std::string, int> doc_versions_;
};
