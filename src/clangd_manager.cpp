#include "clangd_manager.h"
#include <iostream>
#include <lsp/connection.h>
#include <lsp/messagehandler.h>
#include <lsp/messages.h>
#include <lsp/process.h>
#include <lsp/io/socket.h>
#include <lsp/io/standardio.h>
#include <filesystem>
#include "event_logger.h"

namespace fs = std::filesystem;

clangd_manager &clangd_manager::get_instance()
{
	static clangd_manager instance;
	return instance;
}

clangd_manager::~clangd_manager()
{
	stop();
}

void clangd_manager::start(event_queue &queue)
{
	global_queue_ = &queue;
	
	try {
		// Launch clangd with error logging only to prevent stderr leakage onto the ncurses screen
		process_ = std::make_unique<lsp::Process>("clangd", std::vector<std::string>{"-log=error"});
		connection_ = std::make_unique<lsp::Connection>(process_->stdIO());
		message_handler_ = std::make_unique<lsp::MessageHandler>(*connection_);
		
		is_running_.store(true);
		message_thread_ = std::thread([this]() { message_loop(); });

		auto initializeParams = lsp::requests::Initialize::Params();
		initializeParams.processId = lsp::Process::currentProcessId();
		// Use current working directory as root
		std::string cwd = fs::current_path().string();
		initializeParams.rootUri = lsp::DocumentUri::fromPath(cwd);
		initializeParams.capabilities = {
			.textDocument = lsp::TextDocumentClientCapabilities{
				.hover = lsp::HoverClientCapabilities{
					.contentFormat = {{lsp::MarkupKind::PlainText}}
				}
			}
		};

		auto initializeRequest = message_handler_->sendRequest<lsp::requests::Initialize>(std::move(initializeParams));
		// We wait for the result
		auto initializeResult = initializeRequest.result.get();

		message_handler_->sendNotification<lsp::notifications::Initialized>({});
		
		message_handler_->add<lsp::notifications::TextDocument_PublishDiagnostics>([this](const lsp::notifications::TextDocument_PublishDiagnostics::Params& params) {
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
				
				// Sort diagnostics to find the first one (lowest line, then lowest character)
				std::sort(diagnostics.begin(), diagnostics.end(), [](const diagnostic_info& a, const diagnostic_info& b) {
					if (a.range.start_y != b.range.start_y) {
						return a.range.start_y < b.range.start_y;
					}
					return a.range.start_x < b.range.start_x;
				});

				// Only keep the first one
				if (diagnostics.size() > 1) {
					diagnostics.resize(1);
				}

				editor_event ev;
				ev.type = event_type::lsp_diagnostics_result;
				ev.diagnostics = diagnostics;
				global_queue_->push(ev);
			}
		});

		event_logger::get_instance().log("clangd started and initialized successfully.");
	} catch (const std::exception& e) {
		event_logger::get_instance().log("Failed to start clangd: " + std::string(e.what()));
		is_running_.store(false);
	}
}

void clangd_manager::stop()
{
	if (!is_running_) return;

	try {
		message_handler_->sendRequest<lsp::requests::Shutdown>(
			[this](const lsp::requests::Shutdown::Result&) {
				message_handler_->sendNotification<lsp::notifications::Exit>();
				is_running_.store(false);
			},
			[this](const lsp::ResponseError& error) {
				event_logger::get_instance().log(std::string("clangd shutdown error: ") + error.message());
				is_running_.store(false);
			}
		);
	} catch (...) {
		is_running_.store(false);
	}

	if (message_thread_.joinable()) {
		message_thread_.join();
	}
	
	message_handler_.reset();
	connection_.reset();
	process_.reset();
}

void clangd_manager::open_document(const std::string &filepath, const std::string &text)
{
	if (!is_running_ || !is_supported_file(filepath)) return;
	
	try {
		std::lock_guard<std::mutex> lock(doc_mutex_);
		doc_versions_[filepath] = 1;

		auto uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		message_handler_->sendNotification<lsp::notifications::TextDocument_DidOpen>({
			.textDocument = {
				.uri = std::move(uri),
				.languageId = "cpp", // Hardcode C++ for now since clangd is C++
				.version = 1,
				.text = text
			}
		});
	} catch (...) {}
}

void clangd_manager::update_document(const std::string &filepath, const std::string &text)
{
	if (!is_running_ || !is_supported_file(filepath)) return;

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
		
		message_handler_->sendNotification<lsp::notifications::TextDocument_DidChange>(std::move(didChangeParams));
	} catch (...) {}
}

void clangd_manager::request_hover(const std::string &filepath, int line, int character)
{
	if (!is_running_ || !is_supported_file(filepath)) return;
	
	try {
		auto hoverParams = lsp::requests::TextDocument_Hover::Params();
		hoverParams.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		hoverParams.position = {static_cast<unsigned int>(line), static_cast<unsigned int>(character)};

		message_handler_->sendRequest<lsp::requests::TextDocument_Hover>(
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
				event_logger::get_instance().log(std::string("clangd hover error: ") + error.message());
			}
		);
	} catch (...) {}
}

void clangd_manager::request_document_highlight(const std::string &filepath, int line, int character)
{
	if (!is_running_ || !is_supported_file(filepath)) return;

	try {
		auto highlightParams = lsp::requests::TextDocument_DocumentHighlight::Params();
		highlightParams.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		highlightParams.position = {static_cast<unsigned int>(line), static_cast<unsigned int>(character)};

		message_handler_->sendRequest<lsp::requests::TextDocument_DocumentHighlight>(
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
					// Clear highlights if result is null (e.g., cursor not on a symbol)
					if (global_queue_) {
						editor_event ev;
						ev.type = event_type::lsp_highlight_result;
						// empty highlight_ranges
						global_queue_->push(ev);
					}
				}
			},
			[](const lsp::ResponseError& error) {
				event_logger::get_instance().log(std::string("clangd highlight error: ") + error.message());
			}
		);
	} catch (...) {}
}

void clangd_manager::request_selection_range(const std::string &filepath, int line, int character)
{
	if (!is_running_ || !is_supported_file(filepath)) return;

	try {
		auto selectionParams = lsp::requests::TextDocument_SelectionRange::Params();
		selectionParams.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		selectionParams.positions.push_back({static_cast<unsigned int>(line), static_cast<unsigned int>(character)});

		message_handler_->sendRequest<lsp::requests::TextDocument_SelectionRange>(
			std::move(selectionParams),
			[this](const lsp::requests::TextDocument_SelectionRange::Result& result) {
				if (!result.isNull() && !result.value().empty()) {
					if (global_queue_) {
						editor_event ev;
						ev.type = event_type::lsp_selection_range_result;
						
						// We traverse the tree from innermost to outermost
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
				event_logger::get_instance().log(std::string("clangd selection range error: ") + error.message());
			}
		);
	} catch (...) {}
}

bool clangd_manager::is_supported_file(const std::string &filepath) const
{
	std::string ext = fs::path(filepath).extension().string();
	// Convert to lowercase
	for (auto &c : ext)
		c = std::tolower(c);
	return (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp");
}

void clangd_manager::message_loop()
{
	try {
		while (is_running_) {
			message_handler_->processIncomingMessages();
		}
	} catch (const std::exception &e) {
		is_running_.store(false);
		event_logger::get_instance().log("clangd message loop error: " + std::string(e.what()));
	}
}
