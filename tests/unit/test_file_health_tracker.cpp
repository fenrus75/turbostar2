// Tested source file: src/agentlib/file_health_utils.h
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

	// 6. Real compile check with clean file and subsequent compiler error (ANSI colored diagnostics)
	{
		std::string main_path = fs_utils::safe_absolute("src/main.cpp").string();
		std::string original_content;
		{
			std::ifstream ifs(main_path);
			original_content.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		}
		struct file_restorer {
			std::string path;
			std::string content;
			~file_restorer() {
				std::ofstream ofs(path);
				ofs << content;
			}
		} restorer{main_path, original_content};

		// 6a. Clean state on clean file
		ctx.file_health_tracker[main_path].state = agentlib::lsp_health_state::unknown;
		ctx.file_health_tracker[main_path].originating_edit_id.clear();
		std::string edit_id1 = agentlib::update_file_health_state(ctx, main_path);
		assert(ctx.file_health_tracker[main_path].state == agentlib::lsp_health_state::clean);
		assert(ctx.file_health_tracker[main_path].originating_edit_id.empty());

		// 6b. Introduce syntax error: append invalid closing brace
		{
			std::ofstream ofs(main_path, std::ios::app);
			ofs << "\n}}\n";
		}
		std::string edit_id2 = agentlib::update_file_health_state(ctx, main_path);
		assert(ctx.file_health_tracker[main_path].state == agentlib::lsp_health_state::dirty);
		assert(ctx.file_health_tracker[main_path].originating_edit_id == edit_id2);

		// Attribution note should blame edit_id2
		std::string note = agentlib::get_file_health_attribution_note(ctx, main_path);
		assert(note.find("after Edit " + edit_id2) != std::string::npos);
	}

	std::cout << "test_file_health_tracker passed successfully." << std::endl;
	return 0;
}
