// Tested source file: src/ui/terminal_window.cpp
#include <cassert>
#include <iostream>
#include <ncurses.h>
#include <string>
#include "config_manager.h"
#include "editor.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "test_watchdog.h"
#include "ui/terminal_window.h"

int main()
{
	test_watchdog::setup_watchdog(30);

	// Test 1: Default is_gdb is false
	ui::terminal_window tw1(1, 0, 0, 80, 24, "Compile Output");
	assert(!tw1.is_gdb());

	// Test 2: set_gdb mutator
	tw1.set_gdb(true);
	assert(tw1.is_gdb());
	tw1.set_gdb(false);
	assert(!tw1.is_gdb());

	// Test 3: Constructor with is_gdb parameter
	ui::terminal_window tw2(2, 0, 0, 80, 24, "Debugger (GDB)", true);
	assert(tw2.is_gdb());

	// Test 4: GDB terminal window with a custom title must have is_gdb == true
	ui::terminal_window tw3(3, 0, 0, 80, 24, "Coredump Debugger (GDB)", true);
	assert(tw3.is_gdb());

	// Test 5: Integration with editor - verify layout handles renamed GDB window via is_gdb()
	std::string python_bin = fs_utils::find_executable("python3");
	if (!python_bin.empty()) {
		project_manager::get_instance().initialize();
		config_manager::get_instance().set_main_executable("python3");

		LINES = 25;
		COLS = 80;

		editor_options opts;
		opts.exit_immediately = 0.0;
		opts.no_welcome = true;
		editor ed(opts);

		auto res = ed.start_app("", true);
		if (res.gdb_run_id > 0) {
			auto *gdb_win = ed.find_terminal_window(res.gdb_run_id);
			assert(gdb_win != nullptr);
			assert(gdb_win->is_gdb());

			// Rename window title: layout must still identify it via is_gdb()
			gdb_win->set_title("Custom GDB Session Title");

			// Trigger layout update via select_window
			editor_event sel_ev;
			sel_ev.type = event_type::select_window;
			sel_ev.key_code = 0;
			ed.dispatch(sel_ev);

			int total_h = LINES - 2;
			int app_h = (total_h * 2) / 3;
			int gdb_h = total_h - app_h;
			assert(gdb_win->get_height() == gdb_h);
			assert(gdb_win->get_y() == 1 + app_h);

			ed.terminate_run(res.app_run_id);
			ed.terminate_run(res.gdb_run_id);
		}
	}

	std::cout << "test_terminal_window passed successfully!" << std::endl;
	return 0;
}
