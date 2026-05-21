#include "lsp_manager.h"
#include <iostream>
#include <lsp/connection.h>
#include <lsp/messagehandler.h>
#include <lsp/messages.h>
#include <lsp/process.h>
#include <lsp/io/socket.h>
#include <lsp/io/standardio.h>
#include <filesystem>
#include "event_logger.h"
#include "fs_utils.h"
#include <signal.h>
#include <algorithm>

namespace fs = std::filesystem;

lsp_manager &lsp_manager::get_instance()
{
	static lsp_manager instance;
	return instance;
}

lsp_manager::~lsp_manager()
{
	stop();
}

void lsp_manager::start_server(const std::string& name, const std::vector<std::string>& args, const std::string& language_id)
{
	auto server = std::make_unique<server_instance>();
	server->language_id = language_id;
	try {
		server->process = std::make_unique<lsp::Process>(name, args);
		server->connection = std::make_unique<lsp::Connection>(server->process->stdIO());
		server->message_handler = std::make_unique<lsp::MessageHandler>(*(server->connection));
		server->is_running.store(true);
		
		server->message_thread = std::thread([s = server.get()]() {
			try {
				while (s->is_running.load()) {
					s->message_handler->processIncomingMessages();
				}
			} catch (const std::exception &e) {
				s->is_running.store(false);
				event_logger::get_instance().log("LSP message loop error: " + std::string(e.what()));
			}
		});

		auto initializeParams = lsp::requests::Initialize::Params();
		initializeParams.processId = lsp::Process::currentProcessId();
		std::string cwd = fs::current_path().string();
		initializeParams.rootUri = lsp::DocumentUri::fromPath(cwd);
		initializeParams.capabilities = {
			.textDocument = lsp::TextDocumentClientCapabilities{
				.hover = lsp::HoverClientCapabilities{
					.contentFormat = {{lsp::MarkupKind::PlainText}}
				}
			}
		};

		auto initializeRequest = server->message_handler->sendRequest<lsp::requests::Initialize>(std::move(initializeParams));
		auto initializeResult = initializeRequest.result.get();

		server->message_handler->sendNotification<lsp::notifications::Initialized>({});
		
		server->message_handler->add<lsp::notifications::TextDocument_PublishDiagnostics>([this](const lsp::notifications::TextDocument_PublishDiagnostics::Params& params) {
			if (global_queue_) {
				std::vector<diagnostic_info> diagnostics;
				for (const auto& diag : params.diagnostics) {
					diagnostic_info info;
					info.range = {
						static_cast<int>(diag.range.start.line),
						static_cast<int>(diag.range.start.character),
						static_cast<int>(diag.range.end.line),
						static_cast<int>(diag.range.end.character)
					};
					info.severity = diag.severity.value_or(lsp::DiagnosticSeverity::Error);
					info.message = diag.message;
					diagnostics.push_back(info);
				}
				
				std::sort(diagnostics.begin(), diagnostics.end(), [](const diagnostic_info& a, const diagnostic_info& b) {
					if (a.range.start_y != b.range.start_y) {
						return a.range.start_y < b.range.start_y;
					}
					return a.range.start_x < b.range.start_x;
				});

				if (diagnostics.size() > 1) {
					diagnostics.resize(1);
				}

				editor_event ev;
				ev.type = event_type::lsp_diagnostics_result;
				ev.diagnostics = diagnostics;
				global_queue_->push(ev);
			}
		});

		servers_.push_back(std::move(server));
		event_logger::get_instance().log(name + " started and initialized successfully.");
	} catch (const std::exception& e) {
		event_logger::get_instance().log("Failed to start " + name + ": " + std::string(e.what()));
		server->is_running.store(false);
	}
}

void lsp_manager::start(event_queue &queue)
{
	global_queue_ = &queue;
	
	start_server("clangd", {"-log=error"}, "cpp");
	start_server("pylsp", {}, "python");
}

void lsp_manager::stop()
{
	for (auto& server : servers_) {
		if (!server->is_running) continue;

		try {
			(void)server->message_handler->sendRequest<lsp::requests::Shutdown>();
		} catch (...) {}

		try {
			server->message_handler->sendNotification<lsp::notifications::Exit>();
		} catch (...) {}

		try {
			if (server->process) {
				kill(server->process->id(), SIGKILL);
				server->process->terminate();
			}
		} catch (...) {}

		server->is_running.store(false);

		if (server->message_thread.joinable()) {
			server->message_thread.join();
		}
	}
	servers_.clear();
}

lsp_manager::server_instance* lsp_manager::get_server_for_file(const std::string& filepath)
{
	std::string ext = fs::path(filepath).extension().string();
	for (auto &c : ext) c = std::tolower(c);
	
	std::string lang_id;
	if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp") lang_id = "cpp";
	else if (ext == ".py") lang_id = "python";
	
	if (lang_id.empty()) return nullptr;
	
	for (auto& server : servers_) {
		if (server->language_id == lang_id && server->is_running) {
			return server.get();
		}
	}
	return nullptr;
}

bool lsp_manager::is_supported_file(const std::string &filepath) const
{
	std::string ext = fs::path(filepath).extension().string();
	for (auto &c : ext) c = std::tolower(c);
	return (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".py");
}

void lsp_manager::open_document(const std::string &filepath, const std::string &text)
{
	auto server = get_server_for_file(filepath);
	if (!server) return;
	
	try {
		std::lock_guard<std::mutex> lock(doc_mutex_);
		doc_versions_[filepath] = 1;

		auto uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		server->message_handler->sendNotification<lsp::notifications::TextDocument_DidOpen>({
			.textDocument = {
				.uri = std::move(uri),
				.languageId = server->language_id,
				.version = 1,
				.text = text
			}
		});
	} catch (...) {}
}

void lsp_manager::update_document(const std::string &filepath, const std::string &text)
{
	auto server = get_server_for_file(filepath);
	if (!server) return;

	try {
		std::lock_guard<std::mutex> lock(doc_mutex_);
		int version = ++doc_versions_[filepath];

		auto uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		lsp::DidChangeTextDocumentParams didChangeParams;
		didChangeParams.textDocument.uri = std::move(uri);
		didChangeParams.textDocument.version = version;
		
		lsp::TextDocumentContentChangeEvent_Text changeEvent;
		changeEvent.text = text;
		didChangeParams.contentChanges.push_back(changeEvent);
		
		server->message_handler->sendNotification<lsp::notifications::TextDocument_DidChange>(std::move(didChangeParams));
	} catch (...) {}
}

void lsp_manager::request_hover(const std::string &filepath, int line, int character)
{
	auto server = get_server_for_file(filepath);
	if (!server) return;
	
	try {
		auto hoverParams = lsp::requests::TextDocument_Hover::Params();
		hoverParams.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		hoverParams.position = {static_cast<unsigned int>(line), static_cast<unsigned int>(character)};

		server->message_handler->sendRequest<lsp::requests::TextDocument_Hover>(
			std::move(hoverParams),
			[this](const lsp::requests::TextDocument_Hover::Result& result) {
				if (!result.isNull()) {
					std::string payload;
					if (const auto* contents = std::get_if<lsp::MarkupContent>(&result->contents)) {
						payload = contents->value;
					}
					
					if (!payload.empty() && global_queue_) {
						editor_event ev;
						ev.type = event_type::lsp_hover_result;
						ev.payload = payload;
						global_queue_->push(ev);
					}
				}
			},
			[](const lsp::ResponseError& error) {
				event_logger::get_instance().log(std::string("LSP hover error: ") + error.message());
			}
		);
	} catch (...) {}
}

void lsp_manager::request_document_highlight(const std::string &filepath, int line, int character)
{
	auto server = get_server_for_file(filepath);
	if (!server) return;

	try {
		auto highlightParams = lsp::requests::TextDocument_DocumentHighlight::Params();
		highlightParams.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		highlightParams.position = {static_cast<unsigned int>(line), static_cast<unsigned int>(character)};

		server->message_handler->sendRequest<lsp::requests::TextDocument_DocumentHighlight>(
			std::move(highlightParams),
			[this](const lsp::requests::TextDocument_DocumentHighlight::Result& result) {
				if (!result.isNull()) {
					if (global_queue_) {
						editor_event ev;
						ev.type = event_type::lsp_highlight_result;
						for (const auto& hl : result.value()) {
							ev.highlight_ranges.push_back({
								static_cast<int>(hl.range.start.line),
								static_cast<int>(hl.range.start.character),
								static_cast<int>(hl.range.end.line),
								static_cast<int>(hl.range.end.character)
							});
						}
						global_queue_->push(ev);
					}
				} else {
					if (global_queue_) {
						editor_event ev;
						ev.type = event_type::lsp_highlight_result;
						global_queue_->push(ev);
					}
				}
			},
			[](const lsp::ResponseError& error) {
				event_logger::get_instance().log(std::string("LSP highlight error: ") + error.message());
			}
		);
	} catch (...) {}
}

void lsp_manager::request_selection_range(const std::string &filepath, int line, int character)
{
	auto server = get_server_for_file(filepath);
	if (!server) return;

	try {
		auto selectionParams = lsp::requests::TextDocument_SelectionRange::Params();
		selectionParams.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		selectionParams.positions.push_back({static_cast<unsigned int>(line), static_cast<unsigned int>(character)});

		server->message_handler->sendRequest<lsp::requests::TextDocument_SelectionRange>(
			std::move(selectionParams),
			[this](const lsp::requests::TextDocument_SelectionRange::Result& result) {
				if (!result.isNull() && !result.value().empty()) {
					if (global_queue_) {
						editor_event ev;
						ev.type = event_type::lsp_selection_range_result;
						
						const lsp::SelectionRange* current = &result.value()[0];
						while (current) {
							ev.highlight_ranges.push_back({
								static_cast<int>(current->range.start.line),
								static_cast<int>(current->range.start.character),
								static_cast<int>(current->range.end.line),
								static_cast<int>(current->range.end.character)
							});
							current = current->parent.get();
						}
						
						global_queue_->push(ev);
					}
				}
			},
			[](const lsp::ResponseError& error) {
				event_logger::get_instance().log(std::string("LSP selection range error: ") + error.message());
			}
		);
	} catch (...) {}
}
