// Tested source file: src/editor_events_lsp.cpp
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "editor.h"
#include "event_queue.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	editor_options opts;
	opts.exit_immediately = 0.0;
	opts.no_welcome = true;
	opts.no_lsp = true;
	// docs/todo.md is opened last, so it becomes the active document
	opts.filenames = {"src/main.cpp", "docs/todo.md"};
	editor ed(opts);

	assert(ed.get_windows().size() >= 2);
	auto main_doc = ed.get_windows()[0]->get_document();
	auto todo_doc = ed.get_windows()[1]->get_document();
	assert(main_doc != nullptr);
	assert(todo_doc != nullptr);

	// Ensure todo_doc is the currently active document
	assert(ed.get_windows()[1]->is_active());

	// Send LSP diagnostics intended for src/main.cpp
	editor_event ev;
	ev.type = event_type::lsp_diagnostics_result;
	ev.payload = fs_utils::safe_absolute("src/main.cpp").string();
	diagnostic_info diag;
	diag.range = text_range{0, 0, 0, 10};
	diag.severity = 1;
	diag.message = "Included header algorithm is not used directly";
	ev.diagnostics.push_back(diag);

	ed.dispatch(ev);

	// Diagnostics for src/main.cpp must NOT appear on docs/todo.md
	assert(todo_doc->get_lsp_diagnostics().empty());

	// Diagnostics must be routed to src/main.cpp
	assert(!main_doc->get_lsp_diagnostics().empty());
	assert(main_doc->get_lsp_diagnostics()[0].message == "Included header algorithm is not used directly");

	std::cout << "test_editor_lsp_diagnostics passed successfully!" << std::endl;
	return 0;
}
