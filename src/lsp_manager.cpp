#include "lsp_manager.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <future>
#include <iostream>
#include <lsp/connection.h>
#include <lsp/io/socket.h>
#include <lsp/io/standardio.h>
#include <lsp/messagehandler.h>
#include <lsp/messages.h>
#include <lsp/process.h>
#include "config_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"

namespace fs = std::filesystem;

lsp_manager::lsp_manager()
{
}

lsp_manager::~lsp_manager()
{
	stop();
}

void lsp_manager::start_server(const std::string &name, const std::vector<std::string> &args, const std::string &language_id)
{
	// Check if the server executable exists directly in /usr/bin before starting
	std::string server_path = "/usr/bin/" + name;
	if (!fs::exists(server_path)) {
		event_logger::get_instance().log("LSP server executable '{}' not found. Skipping start.", server_path);
		return;
	}

	auto server = std::make_shared<server_instance>();
	server->language_id = language_id;
	try {
		// Launch the LSP server with a lower CPU priority using 'nice'
		// This keeps the editor UI responsive during heavy background indexing.
		std::vector<std::string> nice_args = {"-n", "10", name};
		for (const auto &arg : args) {
			nice_args.push_back(arg);
		}

		server->process = std::make_unique<lsp::Process>("nice", nice_args);
		server->connection = std::make_unique<lsp::Connection>(server->process->stdIO());
		server->message_handler = std::make_unique<lsp::MessageHandler>(*(server->connection));
		server->is_running.store(true);

		server->message_thread = std::thread([s = server.get()]() {
			event_logger::get_instance().log("Thread started: lsp_manager message_thread");
			try {
				while (s->is_running.load() && !project_manager::get_instance().is_exiting()) {
					s->message_handler->processIncomingMessages();
				}
			} catch (const std::exception &e) {
				s->is_running.store(false);
				if (!project_manager::get_instance().is_exiting()) {
					event_logger::get_instance().log("LSP message loop error: {}", e.what());
				}
			}
			event_logger::get_instance().log("Thread exited: lsp_manager message_thread");
		});

		auto initializeParams = lsp::requests::Initialize::Params();
		initializeParams.processId = lsp::Process::currentProcessId();
		std::string cwd = fs::current_path().string();
		initializeParams.rootUri = lsp::DocumentUri::fromPath(cwd);
		initializeParams.capabilities = {
		    .textDocument = lsp::TextDocumentClientCapabilities{
			.hover = lsp::HoverClientCapabilities{.contentFormat = {{lsp::MarkupKind::PlainText}}}}};

		auto initializeRequest = server->message_handler->sendRequest<lsp::requests::Initialize>(std::move(initializeParams));
		auto initializeResult = initializeRequest.result.get();

		server->message_handler->sendNotification<lsp::notifications::Initialized>({});

		server->message_handler->add<lsp::notifications::TextDocument_PublishDiagnostics>(
		    [this](const lsp::notifications::TextDocument_PublishDiagnostics::Params &params) {
			    std::vector<diagnostic_info> diagnostics;
			    for (const auto &diag : params.diagnostics) {
				    diagnostic_info info;
				    info.range = {
					static_cast<int>(diag.range.start.line), static_cast<int>(diag.range.start.character),
					static_cast<int>(diag.range.end.line), static_cast<int>(diag.range.end.character)};
				    info.severity = diag.severity.value_or(lsp::DiagnosticSeverity::Error);
				    info.message = diag.message;
				    diagnostics.push_back(info);
			    }

			    std::sort(diagnostics.begin(), diagnostics.end(),
				      [](const diagnostic_info &a, const diagnostic_info &b) {
					      if (a.range.start_y != b.range.start_y) {
						      return a.range.start_y < b.range.start_y;
					      }
					      return a.range.start_x < b.range.start_x;
				      });

			    std::string path_str = std::string(params.uri.path());
			    store_file_diagnostics(path_str, diagnostics);

			    auto *q = global_queue_.load();
			    if (q) {
				    editor_event ev;
				    ev.type = event_type::lsp_diagnostics_result;
				    ev.diagnostics = diagnostics;
				    q->push(ev);
			    }
		    });

		servers_.push_back(std::move(server));
		event_logger::get_instance().log("{} started and initialized successfully.", name);
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to start {}: {}", name, e.what());
		server->is_running.store(false);
	}
}

void lsp_manager::start(event_queue &queue)
{
	global_queue_.store(&queue);
}

void lsp_manager::stop()
{
	global_queue_.store(nullptr);
	std::lock_guard<std::mutex> lock(servers_mutex_);

	// Terminate all active language server processes in parallel.
	// This prevents slow individual language servers from sequentially blocking the main thread,
	// reducing overall exit latency to the maximum teardown duration of a single server.
	std::vector<std::thread> stop_threads;
	for (auto &server : servers_) {
		if (!server->is_running)
			continue;

		stop_threads.push_back(std::thread([server]() {
			try {
				(void)server->message_handler->sendRequest<lsp::requests::Shutdown>();
			} catch (...) {
				// ignore
			}

			try {
				server->message_handler->sendNotification<lsp::notifications::Exit>();
			} catch (...) {
				// ignore
			}

			// Move the process unique_ptr to a detached thread to destroy it asynchronously.
			// This closes the pipes, forcing the child process to terminate immediately (via SIGKILL in destructor)
			// and reap in the background without blocking the editor exit path.
			if (server->process) {
				std::thread([p = std::move(server->process)]() {
					// Destructor of Process runs here in the background
				}).detach();
			}

			server->is_running.store(false);

			if (server->message_thread.joinable()) {
				server->message_thread.join();
			}
		}));
	}

	// Join all server termination helper threads before clearing the registry.
	for (auto &t : stop_threads) {
		if (t.joinable()) {
			t.join();
		}
	}
	servers_.clear();
}

std::shared_ptr<lsp_manager::server_instance> lsp_manager::get_server_for_file(const std::string &filepath)
{
	std::string ext = fs::path(filepath).extension().string();
	for (auto &c : ext)
		c = std::tolower(c);

	std::string lang_id;
	if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp")
		lang_id = "cpp";
	else if (ext == ".py")
		lang_id = "python";

	if (lang_id.empty())
		return nullptr;

	std::lock_guard<std::mutex> lock(servers_mutex_);
	for (auto &server : servers_) {
		if (server->language_id == lang_id && server->is_running) {
			return server;
		}
	}

	// Try to start the server on demand
	if (lang_id == "cpp") {
		std::string build_dir = config_manager::get_instance().get_build_directory();
		unsigned int num_cores = std::thread::hardware_concurrency();
		unsigned int clangd_jobs = std::clamp(num_cores / 2, 2u, 8u);
		start_server("clangd", {"-log=error", "--compile-commands-dir=" + build_dir, std::format("-j={}", clangd_jobs), "--malloc-trim", "--background-index"},
			     "cpp");
	} else if (lang_id == "python") {
		start_server("pylsp", {}, "python");
	}

	for (auto &server : servers_) {
		if (server->language_id == lang_id && server->is_running) {
			return server;
		}
	}

	return nullptr;
}

bool lsp_manager::is_supported_file(const std::string &filepath) const
{
	std::string ext = fs::path(filepath).extension().string();
	for (auto &c : ext)
		c = std::tolower(c);
	return (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".py");
}

std::vector<text_range> lsp_manager::query_selection_ranges(const std::string &filepath, int line, int character)
{
	auto server = get_server_for_file(filepath);
	if (!server)
		return {};

	auto promise = std::make_shared<std::promise<std::vector<text_range>>>();
	auto future = promise->get_future();

	try {
		auto selectionParams = lsp::requests::TextDocument_SelectionRange::Params();
		selectionParams.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		selectionParams.positions.push_back({static_cast<unsigned int>(line), static_cast<unsigned int>(character)});

		server->message_handler->sendRequest<lsp::requests::TextDocument_SelectionRange>(
		    std::move(selectionParams),
		    [promise](const lsp::requests::TextDocument_SelectionRange::Result &result) {
			    std::vector<text_range> ranges;
			    if (!result.isNull() && !result.value().empty()) {
				    const lsp::SelectionRange *current = &result.value()[0];
				    while (current) {
					    ranges.push_back({static_cast<int>(current->range.start.line),
							      static_cast<int>(current->range.start.character),
							      static_cast<int>(current->range.end.line),
							      static_cast<int>(current->range.end.character)});
					    current = current->parent.get();
				    }
			    }
			    promise->set_value(std::move(ranges));
		    },
		    [promise](const lsp::ResponseError & /*error*/) { promise->set_value({}); });

		if (future.wait_for(std::chrono::seconds(2)) == std::future_status::ready) {
			return future.get();
		}
	} catch (...) {
		event_logger::get_instance().log("LSP: Caught unknown exception");
	}

	return {};
}

std::vector<lsp_manager::location_info> lsp_manager::query_definition(const std::string &filepath, int line, int character)
{
	auto server = get_server_for_file(filepath);
	if (!server)
		return {};

	event_logger::get_instance().log(std::format("LSP: query_definition path='{}:L{}'", filepath, line));

	auto promise = std::make_shared<std::promise<std::vector<location_info>>>();
	auto future = promise->get_future();

	try {
		auto params = lsp::requests::TextDocument_Definition::Params();
		params.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		params.position = {static_cast<unsigned int>(line), static_cast<unsigned int>(character)};

		server->message_handler->sendRequest<lsp::requests::TextDocument_Definition>(
		    std::move(params),
		    [promise](const lsp::requests::TextDocument_Definition::Result &result) {
			    std::vector<location_info> infos;
			    if (!result.isNull()) {
				    auto process_location = [&](const lsp::Location &loc) {
					    location_info info;
					    info.path = std::string(loc.uri.path());
					    info.range = {static_cast<int>(loc.range.start.line),
							  static_cast<int>(loc.range.start.character), static_cast<int>(loc.range.end.line),
							  static_cast<int>(loc.range.end.character)};
					    infos.push_back(info);
				    };

				    const auto &val = result.value();
				    if (std::holds_alternative<lsp::Definition>(val)) {
					    const auto &def = std::get<lsp::Definition>(val);
					    if (std::holds_alternative<lsp::Location>(def)) {
						    process_location(std::get<lsp::Location>(def));
					    } else if (std::holds_alternative<std::vector<lsp::Location>>(def)) {
						    for (const auto &l : std::get<std::vector<lsp::Location>>(def))
							    process_location(l);
					    }
				    } else if (std::holds_alternative<std::vector<lsp::DefinitionLink>>(val)) {
					    for (const auto &link : std::get<std::vector<lsp::DefinitionLink>>(val)) {
						    location_info info;
						    info.path = std::string(link.targetUri.path());
						    info.range = {static_cast<int>(link.targetRange.start.line),
								  static_cast<int>(link.targetRange.start.character),
								  static_cast<int>(link.targetRange.end.line),
								  static_cast<int>(link.targetRange.end.character)};
						    infos.push_back(info);
					    }
				    }
			    }
			    promise->set_value(std::move(infos));
		    },
		    [promise](const lsp::ResponseError & /*error*/) { promise->set_value({}); });

		if (future.wait_for(std::chrono::seconds(2)) == std::future_status::ready) {
			return future.get();
		}
	} catch (...) {
		event_logger::get_instance().log("LSP: Caught unknown exception");
	}

	return {};
}

std::vector<lsp_manager::location_info> lsp_manager::query_references(const std::string &filepath, int line, int character)
{
	auto server = get_server_for_file(filepath);
	if (!server)
		return {};

	auto promise = std::make_shared<std::promise<std::vector<location_info>>>();
	auto future = promise->get_future();

	try {
		auto params = lsp::requests::TextDocument_References::Params();
		params.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		params.position = {static_cast<unsigned int>(line), static_cast<unsigned int>(character)};
		params.context.includeDeclaration = true;

		server->message_handler->sendRequest<lsp::requests::TextDocument_References>(
		    std::move(params),
		    [promise](const lsp::requests::TextDocument_References::Result &result) {
			    std::vector<location_info> infos;
			    if (!result.isNull()) {
				    for (const auto &loc : result.value()) {
					    location_info info;
					    info.path = std::string(loc.uri.path());
					    info.range = {static_cast<int>(loc.range.start.line),
							  static_cast<int>(loc.range.start.character), static_cast<int>(loc.range.end.line),
							  static_cast<int>(loc.range.end.character)};
					    infos.push_back(info);
				    }
			    }
			    promise->set_value(std::move(infos));
		    },
		    [promise](const lsp::ResponseError & /*error*/) { promise->set_value({}); });

		if (future.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
			return future.get();
		}
	} catch (...) {
		event_logger::get_instance().log("LSP: Caught unknown exception");
	}

	return {};
}

void lsp_manager::open_document(const std::string &filepath, const std::string &text)
{
	auto server = get_server_for_file(filepath);
	if (!server)
		return;

	try {
		std::lock_guard<std::mutex> lock(doc_mutex_);
		doc_versions_[filepath] = 1;

		auto uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		server->message_handler->sendNotification<lsp::notifications::TextDocument_DidOpen>(
		    {.textDocument = {.uri = std::move(uri), .languageId = server->language_id, .version = 1, .text = text}});
	} catch (...) {
		event_logger::get_instance().log("LSP: Caught unknown exception");
	}
}

void lsp_manager::update_document(const std::string &filepath, const std::string &text)
{
	auto server = get_server_for_file(filepath);
	if (!server)
		return;

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
	} catch (...) {
		event_logger::get_instance().log("LSP: Caught unknown exception");
	}
}

void lsp_manager::request_hover(const std::string &filepath, int line, int character)
{
	auto server = get_server_for_file(filepath);
	if (!server)
		return;

	try {
		auto hoverParams = lsp::requests::TextDocument_Hover::Params();
		hoverParams.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		hoverParams.position = {static_cast<unsigned int>(line), static_cast<unsigned int>(character)};

		server->message_handler->sendRequest<lsp::requests::TextDocument_Hover>(
		    std::move(hoverParams),
		    [this](const lsp::requests::TextDocument_Hover::Result &result) {
			    if (!result.isNull()) {
				    std::string payload;
				    if (const auto *contents = std::get_if<lsp::MarkupContent>(&result->contents)) {
					    payload = contents->value;
				    }

				    auto *q = global_queue_.load();
				    if (!payload.empty() && q) {
					    editor_event ev;
					    ev.type = event_type::lsp_hover_result;
					    ev.payload = payload;
					    q->push(ev);
				    }
			    }
		    },
		    [](const lsp::ResponseError &error) { event_logger::get_instance().log("LSP hover error: {}", error.message()); });
	} catch (...) {
		event_logger::get_instance().log("LSP: Caught unknown exception");
	}
}

void lsp_manager::request_document_highlight(const std::string &filepath, int line, int character)
{
	auto server = get_server_for_file(filepath);
	if (!server)
		return;

	try {
		auto highlightParams = lsp::requests::TextDocument_DocumentHighlight::Params();
		highlightParams.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		highlightParams.position = {static_cast<unsigned int>(line), static_cast<unsigned int>(character)};

		server->message_handler->sendRequest<lsp::requests::TextDocument_DocumentHighlight>(
		    std::move(highlightParams),
		    [this](const lsp::requests::TextDocument_DocumentHighlight::Result &result) {
			    auto *q = global_queue_.load();
			    if (!result.isNull()) {
				    if (q) {
					    editor_event ev;
					    ev.type = event_type::lsp_highlight_result;
					    for (const auto &hl : result.value()) {
						    ev.highlight_ranges.push_back(
							{static_cast<int>(hl.range.start.line), static_cast<int>(hl.range.start.character),
							 static_cast<int>(hl.range.end.line), static_cast<int>(hl.range.end.character)});
					    }
					    q->push(ev);
				    }
			    } else {
				    if (q) {
					    editor_event ev;
					    ev.type = event_type::lsp_highlight_result;
					    q->push(ev);
				    }
			    }
		    },
		    [](const lsp::ResponseError &error) { event_logger::get_instance().log("LSP highlight error: {}", error.message()); });
	} catch (...) {
		event_logger::get_instance().log("LSP: Caught unknown exception");
	}
}

void lsp_manager::request_selection_range(const std::string &filepath, int line, int character)
{
	auto server = get_server_for_file(filepath);
	if (!server)
		return;

	try {
		auto selectionParams = lsp::requests::TextDocument_SelectionRange::Params();
		selectionParams.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		selectionParams.positions.push_back({static_cast<unsigned int>(line), static_cast<unsigned int>(character)});

		server->message_handler->sendRequest<lsp::requests::TextDocument_SelectionRange>(
		    std::move(selectionParams),
		    [this](const lsp::requests::TextDocument_SelectionRange::Result &result) {
			    if (!result.isNull() && !result.value().empty()) {
				    auto *q = global_queue_.load();
				    if (q) {
					    editor_event ev;
					    ev.type = event_type::lsp_selection_range_result;

					    const lsp::SelectionRange *current = &result.value()[0];
					    while (current) {
						    ev.highlight_ranges.push_back({static_cast<int>(current->range.start.line),
										   static_cast<int>(current->range.start.character),
										   static_cast<int>(current->range.end.line),
										   static_cast<int>(current->range.end.character)});
						    current = current->parent.get();
					    }

					    q->push(ev);
				    }
			    }
		    },
		    [](const lsp::ResponseError &error) {
			    event_logger::get_instance().log("LSP selection range error: {}", error.message());
		    });
	} catch (...) {
		event_logger::get_instance().log("LSP: Caught unknown exception");
	}
}

std::vector<lsp_manager::symbol_info> lsp_manager::query_workspace_symbols(const std::string &query)
{
	std::shared_ptr<server_instance> active_server;
	{
		std::lock_guard<std::mutex> lock(servers_mutex_);
		for (auto &srv : servers_) {
			if (srv->is_running) {
				active_server = srv;
				break;
			}
		}
	}

	if (!active_server) {
		active_server = get_server_for_file("dummy.cpp");
	}

	if (!active_server)
		return {};

	event_logger::get_instance().log(std::format("LSP: query_workspace_symbols query='{}'", query));

	auto promise = std::make_shared<std::promise<std::vector<symbol_info>>>();
	auto future = promise->get_future();

	try {
		auto params = lsp::requests::Workspace_Symbol::Params();
		params.query = query;

		auto req_start_time = std::chrono::steady_clock::now();
		active_server->message_handler->sendRequest<lsp::requests::Workspace_Symbol>(
		    std::move(params),
		    [promise, query, req_start_time](const lsp::requests::Workspace_Symbol::Result &res) {
			    std::vector<symbol_info> out;
			    try {
				    if (!res.isNull()) {
					    const auto &variant_val = res.value();
					    if (std::holds_alternative<lsp::Array<lsp::SymbolInformation>>(variant_val)) {
						    const auto &arr = std::get<lsp::Array<lsp::SymbolInformation>>(variant_val);
						    for (const auto &sym : arr) {
							    symbol_info info;
							    info.name = sym.name;
							    info.kind = static_cast<int>(sym.kind);
							    info.location.path = sym.location.uri.path();
							    info.location.range = {static_cast<int>(sym.location.range.start.line),
										   static_cast<int>(sym.location.range.start.character),
										   static_cast<int>(sym.location.range.end.line),
										   static_cast<int>(sym.location.range.end.character)};
							    out.push_back(info);
						    }
					    } else if (std::holds_alternative<lsp::Array<lsp::WorkspaceSymbol>>(variant_val)) {
						    const auto &arr = std::get<lsp::Array<lsp::WorkspaceSymbol>>(variant_val);
						    for (const auto &sym : arr) {
							    symbol_info info;
							    info.name = sym.name;
							    info.kind = static_cast<int>(sym.kind);
							    if (std::holds_alternative<lsp::Location>(sym.location)) {
								    const auto &loc = std::get<lsp::Location>(sym.location);
								    info.location.path = loc.uri.path();
								    info.location.range = {static_cast<int>(loc.range.start.line),
											   static_cast<int>(loc.range.start.character),
											   static_cast<int>(loc.range.end.line),
											   static_cast<int>(loc.range.end.character)};
								    out.push_back(info);
							    }
						    }
					    }
				    }
				    auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - req_start_time).count();
				    event_logger::get_instance().log(std::format("LSP: query_workspace_symbols response received query='{}' after {}ms (found {} symbols)", query, dur_ms, out.size()));
				    promise->set_value(out);
			    } catch (...) {
				    event_logger::get_instance().log("LSP: Caught unknown exception");
			    }
		    },
		    [promise](const lsp::ResponseError &err) {
			    (void)err;
			    try {
				    promise->set_value({});
			    } catch (...) {
				    event_logger::get_instance().log("LSP: Caught unknown exception");
			    }
		    });
	} catch (...) {
		try {
			promise->set_value({});
		} catch (...) {
			event_logger::get_instance().log("LSP: Caught unknown exception");
		}
	}

	auto start_time = std::chrono::steady_clock::now();
	if (future.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready) {
		auto result = future.get();
		auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
		event_logger::get_instance().log(std::format("LSP: query_workspace_symbols query='{}' finished in {}ms (found {} symbols)", query, dur_ms, result.size()));
		return result;
	}

	event_logger::get_instance().log(std::format("LSP: query_workspace_symbols query='{}' timed out after 1000ms", query));
	return {};
}

std::vector<lsp_manager::call_hierarchy_item> lsp_manager::query_call_hierarchy_outgoing(const std::string &filepath, int line,
											 int character)
{
	auto server = get_server_for_file(filepath);
	if (!server)
		return {};

	auto promise_prep = std::make_shared<std::promise<lsp::Opt<lsp::Array<lsp::CallHierarchyItem>>>>();
	auto future_prep = promise_prep->get_future();

	try {
		auto params = lsp::requests::TextDocument_PrepareCallHierarchy::Params();
		params.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		params.position.line = static_cast<lsp::uint>(line);
		params.position.character = static_cast<lsp::uint>(character);

		server->message_handler->sendRequest<lsp::requests::TextDocument_PrepareCallHierarchy>(
		    std::move(params),
		    [promise_prep](const lsp::requests::TextDocument_PrepareCallHierarchy::Result &res) {
			    try {
				    if (!res.isNull()) {
					    promise_prep->set_value(res.value());
				    } else {
					    promise_prep->set_value(lsp::Opt<lsp::Array<lsp::CallHierarchyItem>>{});
				    }
			    } catch (...) {
				    event_logger::get_instance().log("LSP: Caught unknown exception");
			    }
		    },
		    [promise_prep](const lsp::ResponseError &err) {
			    (void)err;
			    try {
				    promise_prep->set_value(lsp::Opt<lsp::Array<lsp::CallHierarchyItem>>{});
			    } catch (...) {
				    event_logger::get_instance().log("LSP: Caught unknown exception");
			    }
		    });
	} catch (...) {
		try {
			promise_prep->set_value(lsp::Opt<lsp::Array<lsp::CallHierarchyItem>>{});
		} catch (...) {
			event_logger::get_instance().log("LSP: Caught unknown exception");
		}
	}

	if (future_prep.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
		return {};
	}

	auto prep_res = future_prep.get();
	if (!prep_res || prep_res->empty())
		return {};

	auto promise_out = std::make_shared<std::promise<std::vector<call_hierarchy_item>>>();
	auto future_out = promise_out->get_future();

	try {
		auto params = lsp::requests::CallHierarchy_OutgoingCalls::Params();
		params.item = prep_res->front();

		server->message_handler->sendRequest<lsp::requests::CallHierarchy_OutgoingCalls>(
		    std::move(params),
		    [promise_out](const lsp::requests::CallHierarchy_OutgoingCalls::Result &res) {
			    try {
				    std::vector<call_hierarchy_item> out;
				    if (!res.isNull()) {
					    for (const auto &call : res.value()) {
						    call_hierarchy_item item;
						    item.name = call.to.name;
						    item.kind = static_cast<int>(call.to.kind);
						    if (call.to.detail)
							    item.detail = *call.to.detail;
						    item.uri = call.to.uri.path();
						    item.range = {static_cast<int>(call.to.range.start.line),
								  static_cast<int>(call.to.range.start.character),
								  static_cast<int>(call.to.range.end.line),
								  static_cast<int>(call.to.range.end.character)};
						    item.selection_range = {static_cast<int>(call.to.selectionRange.start.line),
									    static_cast<int>(call.to.selectionRange.start.character),
									    static_cast<int>(call.to.selectionRange.end.line),
									    static_cast<int>(call.to.selectionRange.end.character)};
						    out.push_back(item);
					    }
				    }
				    promise_out->set_value(out);
			    } catch (...) {
				    event_logger::get_instance().log("LSP: Caught unknown exception");
			    }
		    },
		    [promise_out](const lsp::ResponseError &err) {
			    (void)err;
			    try {
				    promise_out->set_value({});
			    } catch (...) {
				    event_logger::get_instance().log("LSP: Caught unknown exception");
			    }
		    });
	} catch (...) {
		try {
			promise_out->set_value({});
		} catch (...) {
			event_logger::get_instance().log("LSP: Caught unknown exception");
		}
	}

	if (future_out.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
		return future_out.get();
	}
	return {};
}

std::vector<lsp_manager::outgoing_call_item> lsp_manager::query_call_hierarchy_outgoing_batch(
	const std::string &filepath,
	const std::vector<std::pair<int, int>> &positions,
	std::chrono::steady_clock::time_point deadline)
{
	if (positions.empty()) {
		return {};
	}
	auto now = std::chrono::steady_clock::now();
	if (now >= deadline) {
		return {};
	}
	auto server = get_server_for_file(filepath);
	if (!server)
		return {};

	event_logger::get_instance().log(
		std::format("LSP: query_call_hierarchy_outgoing_batch path='{}' ({} positions)", filepath, positions.size()));

	struct prep_entry {
		int line;
		int character;
		std::shared_ptr<std::promise<lsp::Opt<lsp::Array<lsp::CallHierarchyItem>>>> promise;
	};
	std::vector<prep_entry> prep_entries;
	prep_entries.reserve(positions.size());

	std::string abs_path = fs::absolute(filepath).string();

	for (const auto &pos : positions) {
		auto promise_prep = std::make_shared<std::promise<lsp::Opt<lsp::Array<lsp::CallHierarchyItem>>>>();
		prep_entries.push_back({pos.first, pos.second, promise_prep});

		try {
			auto params = lsp::requests::TextDocument_PrepareCallHierarchy::Params();
			params.textDocument.uri = lsp::DocumentUri::fromPath(abs_path);
			params.position.line = static_cast<lsp::uint>(pos.first);
			params.position.character = static_cast<lsp::uint>(pos.second);

			auto req_start_time = std::chrono::steady_clock::now();
			int target_line = pos.first;
			server->message_handler->sendRequest<lsp::requests::TextDocument_PrepareCallHierarchy>(
			    std::move(params),
			    [promise_prep, filepath, target_line, req_start_time](const lsp::requests::TextDocument_PrepareCallHierarchy::Result &res) {
				    try {
					    auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - req_start_time).count();
					    bool has_items = !res.isNull() && !res.value().empty();
					    event_logger::get_instance().log(std::format("LSP: prepareCallHierarchy response received path='{}' line={} after {}ms (found={})", filepath, target_line, dur_ms, has_items));
					    if (!res.isNull()) {
						    promise_prep->set_value(res.value());
					    } else {
						    promise_prep->set_value(lsp::Opt<lsp::Array<lsp::CallHierarchyItem>>{});
					    }
				    } catch (...) {
					    event_logger::get_instance().log("LSP: Caught unknown exception");
				    }
			    },
			    [promise_prep](const lsp::ResponseError &err) {
				    (void)err;
				    try {
					    promise_prep->set_value(lsp::Opt<lsp::Array<lsp::CallHierarchyItem>>{});
				    } catch (...) {
					    event_logger::get_instance().log("LSP: Caught unknown exception");
				    }
			    });
		} catch (...) {
			try {
				promise_prep->set_value(lsp::Opt<lsp::Array<lsp::CallHierarchyItem>>{});
			} catch (...) {
				event_logger::get_instance().log("LSP: Caught unknown exception");
			}
		}
	}

	int rem_prep_ms = std::clamp<int>(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()), 1, 1000);
	auto deadline_prep = std::chrono::steady_clock::now() + std::chrono::milliseconds(rem_prep_ms);

	struct call_entry {
		int line;
		std::shared_ptr<std::promise<std::vector<call_hierarchy_item>>> promise;
	};
	std::vector<call_entry> call_entries;

	for (auto &pe : prep_entries) {
		auto fut = pe.promise->get_future();
		if (fut.wait_until(deadline_prep) == std::future_status::ready) {
			auto prep_res = fut.get();
			if (prep_res && !prep_res->empty()) {
				auto promise_out = std::make_shared<std::promise<std::vector<call_hierarchy_item>>>();
				call_entries.push_back({pe.line, promise_out});

				try {
					auto params = lsp::requests::CallHierarchy_OutgoingCalls::Params();
					params.item = prep_res->front();

					auto req_out_start_time = std::chrono::steady_clock::now();
					int caller_line = pe.line;
					server->message_handler->sendRequest<lsp::requests::CallHierarchy_OutgoingCalls>(
					    std::move(params),
					    [promise_out, filepath, caller_line, req_out_start_time](const lsp::requests::CallHierarchy_OutgoingCalls::Result &res) {
						    try {
							    std::vector<call_hierarchy_item> out;
							    if (!res.isNull()) {
								    for (const auto &call : res.value()) {
									    call_hierarchy_item item;
									    item.name = call.to.name;
									    item.kind = static_cast<int>(call.to.kind);
									    if (call.to.detail)
										    item.detail = *call.to.detail;
									    item.uri = call.to.uri.path();
									    item.range = {static_cast<int>(call.to.range.start.line),
											  static_cast<int>(call.to.range.start.character),
											  static_cast<int>(call.to.range.end.line),
											  static_cast<int>(call.to.range.end.character)};
									    item.selection_range = {static_cast<int>(call.to.selectionRange.start.line),
												    static_cast<int>(call.to.selectionRange.start.character),
												    static_cast<int>(call.to.selectionRange.end.line),
												    static_cast<int>(call.to.selectionRange.end.character)};
									    out.push_back(item);
								    }
							    }
							    auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - req_out_start_time).count();
							    event_logger::get_instance().log(std::format("LSP: outgoingCalls response received path='{}' line={} after {}ms (found {} calls)", filepath, caller_line, dur_ms, out.size()));
							    promise_out->set_value(out);
						    } catch (...) {
							    event_logger::get_instance().log("LSP: Caught unknown exception");
						    }
					    },
					    [promise_out](const lsp::ResponseError &err) {
						    (void)err;
						    try {
							    promise_out->set_value({});
						    } catch (...) {
							    event_logger::get_instance().log("LSP: Caught unknown exception");
						    }
					    });
				} catch (...) {
					try {
						promise_out->set_value({});
					} catch (...) {
						event_logger::get_instance().log("LSP: Caught unknown exception");
					}
				}
			}
		}
	}

	if (call_entries.empty()) {
		return {};
	}

	int rem_out_ms = std::clamp<int>(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()), 1, 1000);
	auto deadline_out = std::chrono::steady_clock::now() + std::chrono::milliseconds(rem_out_ms);
	std::vector<outgoing_call_item> result;

	for (auto &ce : call_entries) {
		auto fut = ce.promise->get_future();
		if (fut.wait_until(deadline_out) == std::future_status::ready) {
			auto items = fut.get();
			for (auto &item : items) {
				result.push_back({ce.line, item});
			}
		}
	}

	return result;
}

std::vector<lsp_manager::type_hierarchy_item> lsp_manager::query_type_hierarchy_supertypes(const std::string &filepath, int line,
											   int character)
{
	auto server = get_server_for_file(filepath);
	if (!server)
		return {};

	auto promise_prep = std::make_shared<std::promise<lsp::Opt<lsp::Array<lsp::TypeHierarchyItem>>>>();
	auto future_prep = promise_prep->get_future();

	try {
		auto params = lsp::requests::TextDocument_PrepareTypeHierarchy::Params();
		params.textDocument.uri = lsp::DocumentUri::fromPath(fs::absolute(filepath).string());
		params.position.line = static_cast<lsp::uint>(line);
		params.position.character = static_cast<lsp::uint>(character);

		server->message_handler->sendRequest<lsp::requests::TextDocument_PrepareTypeHierarchy>(
		    std::move(params),
		    [promise_prep](const lsp::requests::TextDocument_PrepareTypeHierarchy::Result &res) {
			    try {
				    if (!res.isNull()) {
					    promise_prep->set_value(res.value());
				    } else {
					    promise_prep->set_value(lsp::Opt<lsp::Array<lsp::TypeHierarchyItem>>{});
				    }
			    } catch (...) {
				    event_logger::get_instance().log("LSP: Caught unknown exception");
			    }
		    },
		    [promise_prep](const lsp::ResponseError &err) {
			    (void)err;
			    try {
				    promise_prep->set_value(lsp::Opt<lsp::Array<lsp::TypeHierarchyItem>>{});
			    } catch (...) {
				    event_logger::get_instance().log("LSP: Caught unknown exception");
			    }
		    });
	} catch (...) {
		try {
			promise_prep->set_value(lsp::Opt<lsp::Array<lsp::TypeHierarchyItem>>{});
		} catch (...) {
			event_logger::get_instance().log("LSP: Caught unknown exception");
		}
	}

	if (future_prep.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
		return {};
	}

	auto prep_res = future_prep.get();
	if (!prep_res || prep_res->empty())
		return {};

	auto promise_sup = std::make_shared<std::promise<std::vector<type_hierarchy_item>>>();
	auto future_sup = promise_sup->get_future();

	try {
		auto params = lsp::requests::TypeHierarchy_Supertypes::Params();
		params.item = prep_res->front();

		server->message_handler->sendRequest<lsp::requests::TypeHierarchy_Supertypes>(
		    std::move(params),
		    [promise_sup](const lsp::requests::TypeHierarchy_Supertypes::Result &res) {
			    try {
				    std::vector<type_hierarchy_item> out;
				    if (!res.isNull()) {
					    for (const auto &call : res.value()) {
						    type_hierarchy_item item;
						    item.name = call.name;
						    item.kind = static_cast<int>(call.kind);
						    if (call.detail)
							    item.detail = *call.detail;
						    item.uri = call.uri.path();
						    item.range = {static_cast<int>(call.range.start.line),
								  static_cast<int>(call.range.start.character),
								  static_cast<int>(call.range.end.line),
								  static_cast<int>(call.range.end.character)};
						    item.selection_range = {static_cast<int>(call.selectionRange.start.line),
									    static_cast<int>(call.selectionRange.start.character),
									    static_cast<int>(call.selectionRange.end.line),
									    static_cast<int>(call.selectionRange.end.character)};
						    out.push_back(item);
					    }
				    }
				    promise_sup->set_value(out);
			    } catch (...) {
				    event_logger::get_instance().log("LSP: Caught unknown exception");
			    }
		    },
		    [promise_sup](const lsp::ResponseError &err) {
			    (void)err;
			    try {
				    promise_sup->set_value({});
			    } catch (...) {
				    event_logger::get_instance().log("LSP: Caught unknown exception");
			    }
		    });
	} catch (...) {
		try {
			promise_sup->set_value({});
		} catch (...) {
			event_logger::get_instance().log("LSP: Caught unknown exception");
		}
	}

	if (future_sup.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
		return future_sup.get();
	}
	return {};
}

void lsp_manager::invalidate_symbol_cache(const std::string &filepath)
{
	std::string abs_path = std::filesystem::absolute(filepath).string();
	std::lock_guard<std::mutex> lock(symbol_cache_mutex_);
	symbol_cache_.erase(abs_path);
}

std::vector<lsp_manager::symbol_node> lsp_manager::query_document_symbols(const std::string &filepath)
{
	std::string abs_path = std::filesystem::absolute(filepath).string();
	std::filesystem::file_time_type current_mtime{};
	std::error_code ec;
	if (std::filesystem::exists(abs_path, ec)) {
		current_mtime = std::filesystem::last_write_time(abs_path, ec);
	}

	{
		std::lock_guard<std::mutex> lock(symbol_cache_mutex_);
		auto it = symbol_cache_.find(abs_path);
		if (it != symbol_cache_.end() && it->second.last_mtime == current_mtime && !it->second.symbols.empty()) {
			auto now = std::chrono::steady_clock::now();
			if (now - it->second.last_fetch_time < std::chrono::seconds(5)) {
				return it->second.symbols;
			}
			// Cache entry is >= 5 seconds old: update last_fetch_time to avoid duplicate background requests
			// and return cached symbols immediately while triggering asynchronous LSP refresh in background.
			it->second.last_fetch_time = now;
			auto cached_symbols = it->second.symbols;

			auto server = get_server_for_file(filepath);
			if (server) {
				event_logger::get_instance().log(std::format("LSP: background refresh query_document_symbols path='{}'", filepath));
				try {
					auto params = lsp::requests::TextDocument_DocumentSymbol::Params();
					params.textDocument.uri = lsp::DocumentUri::fromPath(abs_path);
					server->message_handler->sendRequest<lsp::requests::TextDocument_DocumentSymbol>(
					    std::move(params),
					    [abs_path, current_mtime, this](const lsp::requests::TextDocument_DocumentSymbol::Result &res) {
						    std::vector<symbol_node> out;
						    try {
							    if (!res.isNull()) {
								    const auto &val = res.value();
								    if (std::holds_alternative<lsp::Array<lsp::DocumentSymbol>>(val)) {
									    const auto &arr = std::get<lsp::Array<lsp::DocumentSymbol>>(val);
									    auto convert_symbol = [](auto &self, const lsp::DocumentSymbol &sym) -> symbol_node {
										    symbol_node node;
										    node.name = sym.name;
										    node.kind = static_cast<int>(sym.kind);
										    node.range = {
											    static_cast<int>(sym.range.start.line),
											    static_cast<int>(sym.range.start.character),
											    static_cast<int>(sym.range.end.line),
											    static_cast<int>(sym.range.end.character)
										    };
										    node.selection_range = {
											    static_cast<int>(sym.selectionRange.start.line),
											    static_cast<int>(sym.selectionRange.start.character),
											    static_cast<int>(sym.selectionRange.end.line),
											    static_cast<int>(sym.selectionRange.end.character)
										    };
										    if (sym.children && !sym.children->empty()) {
											    for (const auto &child : *sym.children) {
												    node.children.push_back(self(self, child));
											    }
										    }
										    return node;
									    };

									    for (const auto &sym : arr) {
										    out.push_back(convert_symbol(convert_symbol, sym));
									    }
								    } else if (std::holds_alternative<lsp::Array<lsp::SymbolInformation>>(val)) {
									    const auto &arr = std::get<lsp::Array<lsp::SymbolInformation>>(val);
									    for (const auto &sym : arr) {
										    symbol_node node;
										    node.name = sym.name;
										    node.kind = static_cast<int>(sym.kind);
										    node.range = {
											    static_cast<int>(sym.location.range.start.line),
											    static_cast<int>(sym.location.range.start.character),
											    static_cast<int>(sym.location.range.end.line),
											    static_cast<int>(sym.location.range.end.character)
										    };
										    node.selection_range = node.range;
										    out.push_back(node);
									    }
								    }
							    }
							    if (!out.empty()) {
								    std::lock_guard<std::mutex> lock(symbol_cache_mutex_);
								    symbol_cache_[abs_path] = {current_mtime, std::chrono::steady_clock::now(), out};
							    }
						    } catch (...) {
							    event_logger::get_instance().log("LSP: Caught unknown exception in background refresh lambda");
						    }
					    },
					    [](const lsp::ResponseError &err) {
						    event_logger::get_instance().log("LSP: background document symbol refresh error: {}", err.message());
					    });
				} catch (...) {}
			}

			return cached_symbols;
		}
	}

	auto server = get_server_for_file(filepath);
	if (!server)
		return {};

	event_logger::get_instance().log(std::format("LSP: query_document_symbols path='{}'", filepath));

	auto promise = std::make_shared<std::promise<std::vector<symbol_node>>>();
	auto future = promise->get_future();

	try {
		auto params = lsp::requests::TextDocument_DocumentSymbol::Params();
		params.textDocument.uri = lsp::DocumentUri::fromPath(abs_path);

		auto req_start_time = std::chrono::steady_clock::now();
		server->message_handler->sendRequest<lsp::requests::TextDocument_DocumentSymbol>(
		    std::move(params),
		    [promise, abs_path, current_mtime, req_start_time, this](const lsp::requests::TextDocument_DocumentSymbol::Result &res) {
			    std::vector<symbol_node> out;
			    try {
				    if (!res.isNull()) {
					    const auto &val = res.value();
					    if (std::holds_alternative<lsp::Array<lsp::DocumentSymbol>>(val)) {
						    const auto &arr = std::get<lsp::Array<lsp::DocumentSymbol>>(val);
						    auto convert_symbol = [](auto &self, const lsp::DocumentSymbol &sym) -> symbol_node {
							    symbol_node node;
							    node.name = sym.name;
							    node.kind = static_cast<int>(sym.kind);
							    node.range = {
								    static_cast<int>(sym.range.start.line),
								    static_cast<int>(sym.range.start.character),
								    static_cast<int>(sym.range.end.line),
								    static_cast<int>(sym.range.end.character)
							    };
							    node.selection_range = {
								    static_cast<int>(sym.selectionRange.start.line),
								    static_cast<int>(sym.selectionRange.start.character),
								    static_cast<int>(sym.selectionRange.end.line),
								    static_cast<int>(sym.selectionRange.end.character)
							    };
							    if (sym.children && !sym.children->empty()) {
								    for (const auto &child : *sym.children) {
									    node.children.push_back(self(self, child));
								    }
							    }
							    return node;
						    };

						    for (const auto &sym : arr) {
							    out.push_back(convert_symbol(convert_symbol, sym));
						    }
					    } else if (std::holds_alternative<lsp::Array<lsp::SymbolInformation>>(val)) {
						    const auto &arr = std::get<lsp::Array<lsp::SymbolInformation>>(val);
						    for (const auto &sym : arr) {
							    symbol_node node;
							    node.name = sym.name;
							    node.kind = static_cast<int>(sym.kind);
							    node.range = {
								    static_cast<int>(sym.location.range.start.line),
								    static_cast<int>(sym.location.range.start.character),
								    static_cast<int>(sym.location.range.end.line),
								    static_cast<int>(sym.location.range.end.character)
							    };
							    node.selection_range = node.range;
							    out.push_back(node);
						    }
					    }
				    }
				    auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - req_start_time).count();
				    event_logger::get_instance().log(std::format("LSP: query_document_symbols response received path='{}' after {}ms (found {} symbols)", abs_path, dur_ms, out.size()));
				    if (!out.empty()) {
					    std::lock_guard<std::mutex> lock(symbol_cache_mutex_);
					    symbol_cache_[abs_path] = {current_mtime, std::chrono::steady_clock::now(), out};
				    }
				    promise->set_value(out);
			    } catch (...) {
				    event_logger::get_instance().log("LSP: Caught unknown exception in query_document_symbols lambda");
				    promise->set_value({});
			    }
		    },
		    [promise](const lsp::ResponseError &err) {
			    event_logger::get_instance().log("LSP: document symbol query error: {}", err.message());
			    try {
				    promise->set_value({});
			    } catch (...) {}
		    });
	} catch (...) {
		event_logger::get_instance().log("LSP: Caught unknown exception in query_document_symbols");
		try {
			promise->set_value({});
		} catch (...) {}
	}

	auto start_time = std::chrono::steady_clock::now();
	if (future.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready) {
		auto result = future.get();
		auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
		event_logger::get_instance().log(std::format("LSP: query_document_symbols path='{}' finished in {}ms (found {} symbols)", filepath, dur_ms, result.size()));
		return result;
	}

	event_logger::get_instance().log(std::format("LSP: query_document_symbols path='{}' timed out after 1000ms", filepath));
	return {};
}

void lsp_manager::store_file_diagnostics(const std::string &filepath, const std::vector<diagnostic_info> &diags)
{
	std::lock_guard<std::mutex> lock(diagnostics_mutex_);
	file_diagnostics_[filepath] = diags;
}

std::optional<std::vector<diagnostic_info>> lsp_manager::query_file_diagnostics(const std::string &filepath)
{
	if (!is_supported_file(filepath)) {
		return std::nullopt;
	}
	auto server = get_server_for_file(filepath);
	if (!server || !server->is_running.load()) {
		return std::nullopt;
	}

	std::lock_guard<std::mutex> lock(diagnostics_mutex_);
	std::string abs_path = std::filesystem::absolute(filepath).string();
	auto it = file_diagnostics_.find(abs_path);
	if (it != file_diagnostics_.end()) {
		return it->second;
	}
	it = file_diagnostics_.find(filepath);
	if (it != file_diagnostics_.end()) {
		return it->second;
	}

	return std::vector<diagnostic_info>{};
}
