#include <cassert>
#include <iostream>
#include "../../src/agentlib/file_health_utils.h"
#include "../../src/lsp_manager.h"
#include "../../src/project_manager.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog();

	agentlib::tool_context ctx;
	std::string test_path = "/tmp/test_file_health.cpp";

	// 1. UNKNOWN -> UNKNOWN (LSP absent)
	std::string edit1 = agentlib::update_file_health_state(ctx, test_path);
	assert(edit1 == "#1");
	assert(ctx.file_health_tracker[test_path].state == agentlib::lsp_health_state::unknown);
	assert(ctx.file_health_tracker[test_path].originating_edit_id.empty());

	// 2. UNKNOWN -> DIRTY
	ctx.file_health_tracker[test_path].state = agentlib::lsp_health_state::unknown;
	// Transition to dirty from unknown does NOT set originating_edit_id
	ctx.file_health_tracker[test_path].state = agentlib::lsp_health_state::dirty;
	assert(ctx.file_health_tracker[test_path].originating_edit_id.empty());

	// 3. CLEAN -> DIRTY
	ctx.file_health_tracker[test_path].state = agentlib::lsp_health_state::clean;
	ctx.file_health_tracker[test_path].originating_edit_id = "#2";
	ctx.file_health_tracker[test_path].state = agentlib::lsp_health_state::dirty;

	std::string note = agentlib::get_file_health_attribution_note(ctx, test_path);
	assert(note.find("File '/tmp/test_file_health.cpp' was clean and first developed errors after Edit #2.") != std::string::npos);

	// 4. DIRTY -> DIRTY (keep originating_edit_id)
	assert(ctx.file_health_tracker[test_path].originating_edit_id == "#2");

	// 5. DIRTY -> CLEAN (reset)
	ctx.file_health_tracker[test_path].state = agentlib::lsp_health_state::clean;
	ctx.file_health_tracker[test_path].originating_edit_id.clear();
	assert(agentlib::get_file_health_attribution_note(ctx, test_path).empty());

	std::cout << "test_file_health_tracker passed successfully." << std::endl;
	return 0;
}
