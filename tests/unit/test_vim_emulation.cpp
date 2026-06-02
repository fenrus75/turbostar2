#include <cassert>
#include <iostream>
#include "../../src/editor.h"

// Friend function to access private members of editor
void test_vim_emulation()
{
	editor_options opts;
	opts.filenames = {};
	opts.no_lsp = true;
	opts.no_welcome = true;

	editor ed(opts);

	// Test 1: Verify initial state
	assert(ed.is_running_ == true);
	assert(ed.active_mode_ == editor::input_mode::normal);
	assert(ed.vim_prefix_mode_ == false);
	std::cout << "Test 1 passed: Initial state verified\n";

	// Test 2: Unknown command should not stop running but log error/transient status
	ed.execute_vim_command("invalid_cmd_xyz");
	assert(ed.is_running_ == true);
	std::cout << "Test 2 passed: Invalid command handled safely\n";

	// Test 3: Command q! should set is_running_ to false
	ed.execute_vim_command("q!");
	assert(ed.is_running_ == false);
	std::cout << "Test 3 passed: q! successfully quits\n";

	// Reset for command w
	ed.is_running_ = true;
	ed.execute_vim_command("w");
	assert(ed.is_running_ == true);
	std::cout << "Test 4 passed: w command processed\n";

	// Test 5: Keypress events for vim prefix mode entry
	// Simulate ESC keypress (27)
	editor_event esc_ev;
	esc_ev.type = event_type::key_press;
	esc_ev.key_code = 27;
	ed.dispatch_event_key(esc_ev);
	assert(ed.vim_prefix_mode_ == true);
	std::cout << "Test 5 passed: Bare ESC enters vim prefix mode\n";

	// Simulate `:` keypress to enter vim prompt
	editor_event colon_ev;
	colon_ev.type = event_type::key_press;
	colon_ev.key_code = ':';
	colon_ev.utf8_char = ":";
	ed.dispatch_event_key(colon_ev);
	assert(ed.vim_prefix_mode_ == false);
	assert(ed.active_mode_ == editor::input_mode::vim);
	std::cout << "Test 6 passed: ':' keypress enters vim prompt mode\n";

	// Simulate character typing in vim prompt
	editor_event q_ev;
	q_ev.type = event_type::key_press;
	q_ev.key_code = 'q';
	q_ev.utf8_char = "q";
	ed.dispatch_event_key(q_ev);
	assert(ed.vim_input_buffer_ == "q");
	std::cout << "Test 7 passed: Characters typed accumulate in buffer\n";

	// Simulate backspace to clear character
	editor_event bs_ev;
	bs_ev.type = event_type::key_press;
	bs_ev.key_code = 127;
	ed.dispatch_event_key(bs_ev);
	assert(ed.vim_input_buffer_ == "");
	assert(ed.active_mode_ == editor::input_mode::vim);
	std::cout << "Test 8 passed: Backspace clears characters\n";

	// Backspace on empty buffer should exit prompt
	ed.dispatch_event_key(bs_ev);
	assert(ed.active_mode_ == editor::input_mode::normal);
	std::cout << "Test 9 passed: Backspace on empty buffer cancels prompt\n";

	std::cout << "All vim_emulation unit tests passed!\n";
}

int main()
{
	test_vim_emulation();
	return 0;
}
