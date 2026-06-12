#include <cassert>
#include <iostream>
#include <limits>
#include <thread>
#include "../../src/editor.h"

// Friend function to access private members of editor
void test_vim_emulation()
{
	editor_options opts;
	opts.filenames = {};
	opts.no_lsp = true;
	opts.no_welcome = true;

	{
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

	// Test 10: Latency tracking initial state
	assert(ed.total_input_events_ == 0);
	assert(ed.total_latency_us_ == 0);
	assert(ed.max_latency_us_ == 0);
	assert(ed.min_latency_us_ == std::numeric_limits<uint64_t>::max());
	std::cout << "Test 10 passed: Latency tracking initial state verified\n";

	// Test 11: Manually simulating event latency updates
	// First event: 500 us (0.5ms)
	{
		uint64_t dur = 500;
		ed.total_latency_us_ += dur;
		ed.total_input_events_++;
		if (dur > ed.max_latency_us_) ed.max_latency_us_ = dur;
		if (dur < ed.min_latency_us_) ed.min_latency_us_ = dur;
		if (dur > 1000) ed.slow_events_count_1ms_++;
	}
	assert(ed.total_input_events_ == 1);
	assert(ed.total_latency_us_ == 500);
	assert(ed.min_latency_us_ == 500);
	assert(ed.max_latency_us_ == 500);
	assert(ed.slow_events_count_1ms_ == 0);

	// Second event: 1500 us (1.5ms)
	{
		uint64_t dur = 1500;
		ed.total_latency_us_ += dur;
		ed.total_input_events_++;
		if (dur > ed.max_latency_us_) ed.max_latency_us_ = dur;
		if (dur < ed.min_latency_us_) ed.min_latency_us_ = dur;
		if (dur > 1000) ed.slow_events_count_1ms_++;
		if (dur > 5000) ed.slow_events_count_5ms_++;
		if (dur > 10000) ed.slow_events_count_10ms_++;
	}
	assert(ed.total_input_events_ == 2);
	assert(ed.total_latency_us_ == 2000);
	assert(ed.min_latency_us_ == 500);
	assert(ed.max_latency_us_ == 1500);
	assert(ed.slow_events_count_1ms_ == 1);
	assert(ed.slow_events_count_5ms_ == 0);
	std::cout << "Test 11 passed: Latency tracking manual simulation verified\n";

	// Verify report printout doesn't crash
	std::cout << "--- Simulating printing report (should show 2 events, 50% > 1ms) ---\n";
	ed.print_latency_report();
	}

	// Test 12: Print report with 0 events
	{
		editor ed_empty(opts);
		std::cout << "--- Simulating printing empty report ---\n";
		ed_empty.print_latency_report();
		std::cout << "Test 12 passed: Empty report printed safely without crash\n";
	}

	std::cout << "All vim_emulation unit tests passed!\n";
}

void test_status_priorities()
{
	editor_options opts;
	opts.filenames = {};
	opts.no_lsp = true;
	opts.no_welcome = true;

	editor ed(opts);

	// Test priority override: HOVER vs INFO vs WARNING
	// Initially no message
	assert(ed.get_active_status_message() == "");

	// Set HOVER (priority 10)
	ed.set_status_message("Hover Text", status_priorities::HOVER);
	assert(ed.get_active_status_message() == "Hover Text");

	// Set INFO (priority 20) - should override HOVER
	ed.set_status_message("Info Text", status_priorities::INFO);
	assert(ed.get_active_status_message() == "Info Text");

	// Set WARNING (priority 30) - should override INFO
	ed.set_status_message("Warning Text", status_priorities::WARNING);
	assert(ed.get_active_status_message() == "Warning Text");

	// Clear WARNING - should fall back to INFO
	ed.clear_status_message(status_priorities::WARNING);
	assert(ed.get_active_status_message() == "Info Text");

	// Clear INFO - should fall back to HOVER
	ed.clear_status_message(status_priorities::INFO);
	assert(ed.get_active_status_message() == "Hover Text");

	// Clear HOVER - should be empty
	ed.clear_status_message(status_priorities::HOVER);
	assert(ed.get_active_status_message() == "");

	// Test expiry: transient status message
	ed.set_status_message("Transient Info", status_priorities::INFO, std::chrono::milliseconds(50));
	assert(ed.get_active_status_message() == "Transient Info");
	
	// Wait for expiry
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	assert(ed.get_active_status_message() == "");

	std::cout << "All status_priorities unit tests passed!\n";
}

void test_status_bar_paste()
{
	editor_options opts;
	opts.filenames = {};
	opts.no_lsp = true;
	opts.no_welcome = true;

	editor ed(opts);

	// Test 1: Paste while in searching mode
	ed.active_mode_ = editor::input_mode::searching;
	ed.search_input_buffer_ = "";
	editor_event paste_ev1;
	paste_ev1.type = event_type::paste;
	paste_ev1.payload = "hello search";
	ed.dispatch(paste_ev1);
	assert(ed.search_input_buffer_ == "hello search");
	std::cout << "Test status_bar_paste (searching) passed!\n";

	// Test 2: Paste while in going_to_line mode
	ed.active_mode_ = editor::input_mode::going_to_line;
	ed.line_input_buffer_ = "";
	editor_event paste_ev2;
	paste_ev2.type = event_type::paste;
	paste_ev2.payload = "12a34";
	ed.dispatch(paste_ev2);
	assert(ed.line_input_buffer_ == "1234");
	std::cout << "Test status_bar_paste (going_to_line) passed!\n";

	// Test 3: Paste while in inline_agent mode
	ed.active_mode_ = editor::input_mode::inline_agent;
	ed.inline_agent_input_buffer_ = "";
	editor_event paste_ev3;
	paste_ev3.type = event_type::paste;
	paste_ev3.payload = "some instruction";
	ed.dispatch(paste_ev3);
	assert(ed.inline_agent_input_buffer_ == "some instruction");
	std::cout << "Test status_bar_paste (inline_agent) passed!\n";

	// Test 4: Paste while in vim mode
	ed.active_mode_ = editor::input_mode::vim;
	ed.vim_input_buffer_ = "";
	editor_event paste_ev4;
	paste_ev4.type = event_type::paste;
	paste_ev4.payload = "w!";
	ed.dispatch(paste_ev4);
	assert(ed.vim_input_buffer_ == "w!");
	std::cout << "Test status_bar_paste (vim) passed!\n";
}

int main()
{
	test_vim_emulation();
	test_status_priorities();
	test_status_bar_paste();
	return 0;
}

