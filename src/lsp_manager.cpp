#include "lsp_manager.h"
#include <algorithm>
#include <filesystem>
#include <future>
#include <iostream>
#include <lsp/connection.h>
#include <lsp/io/socket.h>
#include <lsp/io/standardio.h>
#include <lsp/messagehandler.h>
#include <lsp/messages.h>
#include <lsp/process.h>
#include <signal.h>
#include "config_manager.h"
#include "event_logger.h"
#include "fs_utils.h"

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
	auto server = std::make_unique<server_instance>();
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
				while (s->is_running.load()) {
					s->message_handler->processIncomingMessages();
				}
			} catch (const std::exception &e) {
				s->is_running.store(false);
				event_logger::get_instance().log("LSP message loop error: " + std::string(e.what()));
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
			    if (global_queue_) {
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
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to start " + name + ": " + std::string(e.what()));
		server->is_running.store(false);
	}
}

void lsp_manager::start(event_queue &queue)
{
	global_queue_ = &queue;
}

void lsp_manager::stop()
{
	std::lock_guard<std::mutex> lock(servers_mutex_);
	for (auto &server : servers_) {
		if (!server->is_running)
			continue;

		try {
			(void)server->message_handler->sendRequest<lsp::requests::Shutdown>();
		} catch (...) {
		}

		try {
			server->message_handler->sendNotification<lsp::notifications::Exit>();
		} catch (...) {
		}

		try {
			if (server->process) {
				kill(server->process->id(), SIGKILL);
				server->process->terminate();
			}
		} catch (...) {
		}

		server->is_running.store(false);

		if (server->message_thread.joinable()) {
			server->message_thread.join();
		}
	}
	servers_.clear();
}

lsp_manager::server_instance *lsp_manager::get_server_for_file(const std::string &filepath)
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
			return server.get();
		}
	}

	// Try to start the server on demand
	if (lang_id == "cpp") {
		std::string build_dir = config_manager::get_instance().get_build_directory();
		start_server("clangd", {"-log=error", "--compile-commands-dir=" + build_dir, "-j=4", "--malloc-trim", "--background-index"}, "cpp");
	} else if (lang_id == "python") {
		start_server("pylsp", {}, "python");
	}

	for (auto &server : servers_) {
		if (server->language_id == lang_id && server->is_running) {
			return server.get();
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
	}

	return {};
}

std::vector<lsp_manager::location_info> lsp_manager::query_definition(const std::string &filepath, int line, int character)
{
	auto server = get_server_for_file(filepath);
	if (!server)
		return {};

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

				    if (!payload.empty() && global_queue_) {
					    editor_event ev;
					    ev.type = event_type::lsp_hover_result;
					    ev.payload = payload;
					    global_queue_->push(ev);
				    }
			    }
		    },
		    [](const lsp::ResponseError &error) {
			    event_logger::get_instance().log(std::string("LSP hover error: ") + error.message());
		    });
	} catch (...) {
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
			    if (!result.isNull()) {
				    if (global_queue_) {
					    editor_event ev;
					    ev.type = event_type::lsp_highlight_result;
					    for (const auto &hl : result.value()) {
						    ev.highlight_ranges.push_back(
							{static_cast<int>(hl.range.start.line), static_cast<int>(hl.range.start.character),
							 static_cast<int>(hl.range.end.line), static_cast<int>(hl.range.end.character)});
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
		    [](const lsp::ResponseError &error) {
			    event_logger::get_instance().log(std::string("LSP highlight error: ") + error.message());
		    });
	} catch (...) {
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
				    if (global_queue_) {
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

					    global_queue_->push(ev);
				    }
			    }
		    },
		    [](const lsp::ResponseError &error) {
			    event_logger::get_instance().log(std::string("LSP selection range error: ") + error.message());
		    });
	} catch (...) {
	}
}

std::vector<lsp_manager::symbol_info> lsp_manager::query_workspace_symbols(const std::string &query)
{
	server_instance *active_server = nullptr;
	{
		std::lock_guard<std::mutex> lock(servers_mutex_);
		for (auto &srv : servers_) {
			if (srv->is_running) {
				active_server = srv.get();
				break;
			}
		}
	}

	if (!active_server) {
		active_server = get_server_for_file("dummy.cpp");
	}

	if (!active_server)
		return {};

	auto promise = std::make_shared<std::promise<std::vector<symbol_info>>>();
	auto future = promise->get_future();

	try {
		auto params = lsp::requests::Workspace_Symbol::Params();
		params.query = query;

		active_server->message_handler->sendRequest<lsp::requests::Workspace_Symbol>(
		    std::move(params),
		    [promise](const lsp::requests::Workspace_Symbol::Result &res) {
			    std::vector<symbol_info> out;
			    try {
				    if (!res.isNull()) {
					    const auto& variant_val = res.value();
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
				    promise->set_value(out);
			    } catch (...) {}
		    },
		    [promise](const lsp::ResponseError &err) {
			    (void)err;
			    try { promise->set_value({}); } catch (...) {}
		    });
	} catch (...) {
		try { promise->set_value({}); } catch (...) {}
	}

	if (future.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
		return future.get();
	}
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
		params.textDocument.uri = lsp::DocumentUri::fromPath(filepath);
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
			    } catch (...) {}
		    },
		    [promise_prep](const lsp::ResponseError &err) {
			    (void)err;
			    try { promise_prep->set_value(lsp::Opt<lsp::Array<lsp::CallHierarchyItem>>{}); } catch (...) {}
		    });
	} catch (...) {
		try { promise_prep->set_value(lsp::Opt<lsp::Array<lsp::CallHierarchyItem>>{}); } catch (...) {}
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
			    } catch (...) {}
		    },
		    [promise_out](const lsp::ResponseError &err) {
			    (void)err;
			    try { promise_out->set_value({}); } catch (...) {}
		    });
	} catch (...) {
		try { promise_out->set_value({}); } catch (...) {}
	}

	if (future_out.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
		return future_out.get();
	}
	return {};
}

std::vector<lsp_manager::type_hierarchy_item> lsp_manager::query_type_hierarchy_supertypes(const std::string &filepath, int line, int character)
{
	auto server = get_server_for_file(filepath);
	if (!server)
		return {};

	auto promise_prep = std::make_shared<std::promise<lsp::Opt<lsp::Array<lsp::TypeHierarchyItem>>>>();
	auto future_prep = promise_prep->get_future();

	try {
		auto params = lsp::requests::TextDocument_PrepareTypeHierarchy::Params();
		params.textDocument.uri = lsp::DocumentUri::fromPath(filepath);
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
			    } catch (...) {}
		    },
		    [promise_prep](const lsp::ResponseError &err) {
			    (void)err;
			    try { promise_prep->set_value(lsp::Opt<lsp::Array<lsp::TypeHierarchyItem>>{}); } catch (...) {}
		    });
	} catch (...) {
		try { promise_prep->set_value(lsp::Opt<lsp::Array<lsp::TypeHierarchyItem>>{}); } catch (...) {}
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
			    } catch (...) {}
		    },
		    [promise_sup](const lsp::ResponseError &err) {
			    (void)err;
			    try { promise_sup->set_value({}); } catch (...) {}
		    });
	} catch (...) {
		try { promise_sup->set_value({}); } catch (...) {}
	}

	if (future_sup.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
		return future_sup.get();
	}
	return {};
}
