// Tested source file: src/ui/terminal_window.cpp
#include <cassert>
#include <chrono>
#include <iostream>
#include <ncurses.h>
#include <string>
#include <thread>
#include "editor.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "test_watchdog.h"
#include "ui/terminal_window.h"

int main()
{
	test_watchdog::setup_watchdog(30);

	// Test 1: Directly test start_shell and PTY I/O
	{
		ui::terminal_window tw(1, 0, 0, 80, 24, "Terminal");
		assert(!tw.is_alive());
		assert(!tw.is_shell());
		assert(!tw.should_close_on_exit());

		bool started = tw.start_shell();
		assert(started);
		assert(tw.is_alive());
		assert(tw.is_shell());
		assert(tw.should_close_on_exit());
		assert(tw.bypass_global_shortcuts());

		// Send a command to the shell
		editor_event ev;
		ev.type = event_type::key_press;
		ev.key_code = 'e';
		ev.utf8_char = "echo HELLO_TURBOSTAR\r";
		tw.get_queue().push(ev);
		tw.process_events();

		// Read output with timeout
		bool found_text = false;
		for (int attempt = 0; attempt < 50; ++attempt) {
			tw.update_pty();
			auto screenshot = tw.get_screenshot();
			for (const auto &line : screenshot.grid) {
				if (line.find("HELLO_TURBOSTAR") != std::string::npos) {
					found_text = true;
					break;
				}
			}
			if (found_text) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		assert(found_text);

		// Terminate via exit
		editor_event exit_ev;
		exit_ev.type = event_type::key_press;
		exit_ev.key_code = 'x';
		exit_ev.utf8_char = "exit\r";
		tw.get_queue().push(exit_ev);
		tw.process_events();

		for (int attempt = 0; attempt < 50; ++attempt) {
			tw.update_pty();
			if (!tw.is_alive()) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		assert(!tw.is_alive());
	}

	// Test 2: Editor integration - open_terminal event, F6 toggling, auto-close on exit
	{
		project_manager::get_instance().initialize();

		LINES = 25;
		COLS = 80;

		editor_options opts;
		opts.exit_immediately = 0.0;
		opts.no_welcome = true;
		editor ed(opts);

		// Initially, 1 editor window (untitled)
		assert(ed.get_windows().size() == 1);
		int initial_win_id = ed.get_windows()[0]->get_id();

		// Open terminal via event
		editor_event open_ev;
		open_ev.type = event_type::open_terminal;
		ed.dispatch(open_ev);

		assert(ed.get_windows().size() == 2);
		auto *term_win = dynamic_cast<ui::terminal_window *>(ed.get_windows()[1].get());
		assert(term_win != nullptr);
		assert(term_win->is_active());
		assert(term_win->get_title() == "Terminal");
		assert(term_win->is_shell());

		// Test F6 cycling: from terminal back to editor
		editor_event f6_ev;
		f6_ev.type = event_type::key_press;
		f6_ev.key_code = KEY_F(6);
		ed.dispatch(f6_ev);
		while (auto ev = ed.get_global_queue().pop()) {
			ed.dispatch(*ev);
		}

		// Window 0 (editor) should now be active
		assert(ed.get_windows()[0]->is_active());
		assert(!term_win->is_active());

		// Press F6 again: should focus terminal window
		ed.dispatch(f6_ev);
		while (auto ev = ed.get_global_queue().pop()) {
			ed.dispatch(*ev);
		}
		assert(term_win->is_active());

		// Test F5 zoom/maximize
		editor_event f5_ev;
		f5_ev.type = event_type::key_press;
		f5_ev.key_code = KEY_F(5);
		ed.dispatch(f5_ev);
		while (auto ev = ed.get_global_queue().pop()) {
			ed.dispatch(*ev);
		}
		assert(term_win->is_maximized());

		ed.dispatch(f5_ev);
		while (auto ev = ed.get_global_queue().pop()) {
			ed.dispatch(*ev);
		}
		assert(!term_win->is_maximized());

		// Now simulate shell exit
		term_win->stop_process();
		assert(!term_win->is_alive());

		// update_terminal_windows should queue close_window
		ed.update_terminal_windows();

		// Process queued events
		while (auto ev = ed.get_global_queue().pop()) {
			ed.dispatch(*ev);
		}

		// Terminal window should now be closed, only 1 window remaining
		assert(ed.get_windows().size() == 1);
		assert(ed.get_windows()[0]->get_id() == initial_win_id);
		assert(ed.get_windows()[0]->is_active());
	}

	std::cout << "test_terminal_shell passed successfully!" << std::endl;
	return 0;
}
