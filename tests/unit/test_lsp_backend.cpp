// Tested source file: src/lsp_backend.cpp src/standard_lsp_backend.cpp src/lsp_manager.cpp src/project_manager.cpp
#include <cassert>
#include <iostream>
#include <memory>
#include "lsp_backend.h"
#include "lsp_manager.h"
#include "project_manager.h"
#include "standard_lsp_backend.h"
#include "test_watchdog.h"

namespace
{

class mock_lsp_backend : public lsp_backend
{
      public:
	bool start_called{false};
	bool stop_called{false};
	bool open_doc_called{false};
	bool update_doc_called{false};
	bool save_doc_called{false};
	bool hover_called{false};
	bool invalidate_symbol_called{false};
	std::string last_filepath;
	std::string last_saved_text;

	void start(event_queue &) override
	{
		start_called = true;
	}
	void stop() override
	{
		stop_called = true;
	}

	void open_document(const std::string &filepath, const std::string &) override
	{
		open_doc_called = true;
		last_filepath = filepath;
	}

	void update_document(const std::string &filepath, const std::string &) override
	{
		update_doc_called = true;
		last_filepath = filepath;
	}

	void save_document(const std::string &filepath, const std::string &text) override
	{
		save_doc_called = true;
		last_filepath = filepath;
		last_saved_text = text;
	}

	void request_hover(const std::string &, int, int) override
	{
		hover_called = true;
	}
	void request_document_highlight(const std::string &, int, int) override
	{
	}
	void request_selection_range(const std::string &, int, int) override
	{
	}

	[[nodiscard]] bool is_supported_file(const std::string &filepath) const override
	{
		return filepath.ends_with(".custom");
	}

	[[nodiscard]] std::vector<text_range> query_selection_ranges(const std::string &, int, int) override
	{
		return {};
	}

	[[nodiscard]] std::vector<location_info> query_definition(const std::string &filepath, int line, int col) override
	{
		if (filepath == "target.cpp" && line == 42 && col == 10) {
			location_info loc;
			loc.path = "target.h";
			loc.range = {.start_y = 15, .start_x = 0, .end_y = 15, .end_x = 20};
			return {loc};
		}
		return {};
	}

	[[nodiscard]] std::vector<location_info> query_type_definition(const std::string &filepath, int line, int col) override
	{
		if (filepath == "type_target.cpp" && line == 10 && col == 5) {
			location_info loc;
			loc.path = "type_target.h";
			loc.range = {.start_y = 20, .start_x = 0, .end_y = 20, .end_x = 30};
			return {loc};
		}
		return {};
	}

	[[nodiscard]] std::vector<location_info> query_references(const std::string &filepath, int, int) override
	{
		if (filepath == "target.cpp") {
			location_info loc1;
			loc1.path = "user1.cpp";
			loc1.range = {.start_y = 1, .start_x = 0, .end_y = 1, .end_x = 10};
			return {loc1};
		}
		return {};
	}

	[[nodiscard]] std::vector<symbol_info> query_workspace_symbols(const std::string &query) override
	{
		if (query == "test_sym") {
			symbol_info info;
			info.name = "test_sym";
			info.kind = 1;
			info.location.path = "foo.h";
			info.location.range = {.start_y = 5, .start_x = 0, .end_y = 5, .end_x = 10};
			return {info};
		}
		return {};
	}

	[[nodiscard]] std::vector<symbol_node> query_document_symbols(const std::string &filepath) override
	{
		if (filepath == "doc.cpp") {
			symbol_node node;
			node.name = "my_func";
			node.kind = 12;
			node.range = {.start_y = 10, .start_x = 0, .end_y = 20, .end_x = 0};
			return {node};
		}
		return {};
	}

	bool prefetch_symbol_called = false;
	void prefetch_document_symbols(const std::string &) override
	{
		prefetch_symbol_called = true;
	}

	void invalidate_symbol_cache(const std::string &) override
	{
		invalidate_symbol_called = true;
	}

	[[nodiscard]] std::vector<call_hierarchy_item> query_call_hierarchy_outgoing(const std::string &, int, int) override
	{
		return {};
	}

	[[nodiscard]] std::vector<outgoing_call_item> query_call_hierarchy_outgoing_batch(const std::string &,
											  const std::vector<std::pair<int, int>> &,
											  std::chrono::steady_clock::time_point) override
	{
		return {};
	}

	[[nodiscard]] std::vector<type_hierarchy_item> query_type_hierarchy_supertypes(const std::string &, int, int) override
	{
		return {};
	}

	[[nodiscard]] std::optional<std::vector<diagnostic_info>> query_file_diagnostics(const std::string &filepath) override
	{
		if (filepath == "error.cpp") {
			diagnostic_info d;
			d.message = "Syntax error";
			return std::vector<diagnostic_info>{d};
		}
		return std::nullopt;
	}

	void store_file_diagnostics(const std::string &, const std::vector<diagnostic_info> &) override
	{
	}
};

} // namespace

int main()
{
	test_watchdog::setup_watchdog(30);

	std::cout << "Testing lsp_backend and lsp_manager facade..." << std::endl;

	// 1. Verify default lsp_manager backed by standard_lsp_backend
	{
		lsp_manager mgr;
		assert(mgr.get_backend() != nullptr);
		assert(mgr.is_supported_file("src/main.cpp"));
		assert(mgr.is_supported_file("test.py"));
		assert(mgr.is_supported_file("foo.h"));
		assert(!mgr.is_supported_file("readme.md"));
		assert(!mgr.is_supported_file("data.json"));
	}

	// 2. Verify mock backend delegation through lsp_manager
	{
		auto mock = std::make_unique<mock_lsp_backend>();
		auto *mock_ptr = mock.get();

		lsp_manager mgr(std::move(mock));
		assert(mgr.get_backend() == mock_ptr);

		assert(mgr.is_supported_file("sample.custom"));
		assert(!mgr.is_supported_file("sample.cpp"));

		// Document operations
		mgr.open_document("hello.custom", "contents");
		assert(mock_ptr->open_doc_called);
		assert(mock_ptr->last_filepath == "hello.custom");

		mgr.update_document("hello.custom", "contents v2");
		assert(mock_ptr->update_doc_called);

		mgr.save_document("hello.custom", "contents saved");
		assert(mock_ptr->save_doc_called);
		assert(mock_ptr->last_filepath == "hello.custom");
		assert(mock_ptr->last_saved_text == "contents saved");

		mgr.request_hover("hello.custom", 1, 1);
		assert(mock_ptr->hover_called);

		// Synchronous queries
		auto defs = mgr.query_definition("target.cpp", 42, 10);
		assert(defs.size() == 1);
		assert(defs[0].path == "target.h");
		assert(defs[0].range.start_y == 15);

		auto defs_empty = mgr.query_definition("target.cpp", 1, 1);
		assert(defs_empty.empty());

		auto type_defs = mgr.query_type_definition("type_target.cpp", 10, 5);
		assert(type_defs.size() == 1);
		assert(type_defs[0].path == "type_target.h");
		assert(type_defs[0].range.start_y == 20);

		auto refs = mgr.query_references("target.cpp", 0, 0);
		assert(refs.size() == 1);
		assert(refs[0].path == "user1.cpp");

		auto syms = mgr.query_workspace_symbols("test_sym");
		assert(syms.size() == 1);
		assert(syms[0].name == "test_sym");

		auto doc_syms = mgr.query_document_symbols("doc.cpp");
		assert(doc_syms.size() == 1);
		assert(doc_syms[0].name == "my_func");

		mgr.prefetch_document_symbols("doc.cpp");
		assert(mock_ptr->prefetch_symbol_called);

		mgr.invalidate_symbol_cache("doc.cpp");
		assert(mock_ptr->invalidate_symbol_called);

		auto diags = mgr.query_file_diagnostics("error.cpp");
		assert(diags.has_value());
		assert(diags->size() == 1);
		assert(diags->front().message == "Syntax error");

		auto no_diags = mgr.query_file_diagnostics("clean.cpp");
		assert(!no_diags.has_value());
	}

	// 3. Verify backend swapping via set_backend
	{
		lsp_manager mgr;
		assert(mgr.is_supported_file("test.cpp"));

		auto mock = std::make_unique<mock_lsp_backend>();
		auto *mock_ptr = mock.get();
		mgr.set_backend(std::move(mock));
		assert(mgr.get_backend() == mock_ptr);
		assert(mgr.is_supported_file("test.custom"));
		assert(!mgr.is_supported_file("test.cpp"));
	}

	// 4. Verify standard_lsp_backend direct diagnostics caching and idempotency
	{
		standard_lsp_backend backend;
		diagnostic_info d1;
		d1.message = "type error";
		d1.range = {.start_y = 2, .start_x = 5, .end_y = 2, .end_x = 10};

		backend.store_file_diagnostics("sample.cpp", {d1});
		// Unsupported file query returns nullopt
		assert(!backend.query_file_diagnostics("readme.txt").has_value());

		// Multiple stop() calls should be safe and idempotent
		backend.stop();
		backend.stop();
	}

	// 5. Verify project_manager::lsp_save_document delegates to lsp_manager
	{
		auto mock = std::make_unique<mock_lsp_backend>();
		auto *mock_ptr = mock.get();
		project_manager::get_instance().set_lsp_backend_for_testing(std::move(mock));

		project_manager::get_instance().lsp_save_document("test_save.cpp", "int x = 42;\n");
		assert(mock_ptr->save_doc_called);
		assert(mock_ptr->last_filepath == "test_save.cpp");
		assert(mock_ptr->last_saved_text == "int x = 42;\n");
	}

	std::cout << "All lsp_backend tests passed successfully!" << std::endl;
	return 0;
}
