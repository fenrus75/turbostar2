// Tested source file: src/ui/inspector_window.cpp
#include <cassert>
#include <iostream>
#include <ncurses.h>
#include "ui/inspector_window.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog();

	// Initialize dummy curses terminal for drawing tests
	initscr();
	start_color();

	std::cout << "Testing inspector_window initialization..." << std::endl;
	inspector_window insp(1, 0, 10, 80, 5);
	assert(insp.get_id() == 1);
	assert(insp.get_x() == 0);
	assert(insp.get_y() == 10);
	assert(insp.get_width() == 80);
	assert(insp.get_height() == 5);
	assert(insp.get_state() == inspector_state::idle);
	assert(insp.get_token().empty());
	assert(insp.get_signature().empty());
	assert(insp.get_proposals().empty());
	assert(insp.get_location().empty());
	assert(insp.bypass_global_shortcuts() == true);

	std::cout << "Testing draw_content and draw_border in idle state..." << std::endl;
	insp.draw_border();
	insp.draw_content(false);
	insp.draw_content(true);

	std::cout << "Testing set_resolving..." << std::endl;
	insp.set_resolving("calculate_hash");
	assert(insp.get_state() == inspector_state::resolving);
	assert(insp.get_token() == "calculate_hash");
	assert(insp.get_signature().empty());
	assert(insp.get_proposals().empty());
	assert(insp.get_location().empty());
	insp.draw_content(false);

	std::cout << "Testing set_active_info..." << std::endl;
	std::vector<turbostar::call_suggestion> proposals = {
		{"(\"data\", 4)", "(const char *data, size_t len)", 0, false},
		{"(sv)", "(std::string_view sv)", 0, false}
	};
	insp.set_active_info("calculate_hash", "uint64_t calculate_hash(const char *data, size_t len)", proposals, "src/hash.h:15");
	assert(insp.get_state() == inspector_state::active);
	assert(insp.get_token() == "calculate_hash");
	assert(insp.get_signature() == "uint64_t calculate_hash(const char *data, size_t len)");
	assert(insp.get_proposals().size() == 2);
	assert(insp.get_proposals()[0].call_text == "(\"data\", 4)");
	assert(insp.get_location() == "src/hash.h:15");
	insp.draw_content(false);

	std::cout << "Testing set_blanked..." << std::endl;
	insp.set_blanked();
	assert(insp.get_state() == inspector_state::blanked);
	insp.draw_content(false);

	std::cout << "Testing set_idle..." << std::endl;
	insp.set_idle();
	assert(insp.get_state() == inspector_state::idle);
	assert(insp.get_token().empty());
	assert(insp.get_signature().empty());
	assert(insp.get_proposals().empty());
	assert(insp.get_location().empty());
	insp.draw_content(false);

	std::cout << "Testing bounds resizing..." << std::endl;
	insp.set_bounds(0, 15, 100, 6);
	assert(insp.get_x() == 0);
	assert(insp.get_y() == 15);
	assert(insp.get_width() == 100);
	assert(insp.get_height() == 6);

	std::cout << "Testing Markdown Lite line parsing..." << std::endl;
	auto toks = inspector_window::parse_markdown_line("**Signature:** `int foo(int x)`");
	assert(!toks.empty());
	assert(toks[0].text == "Signature:");
	assert(toks[0].color_pair == 8);
	assert((toks[0].attr & A_BOLD) != 0);

	auto num_toks = inspector_window::parse_markdown_line("**Args:** 1. `x`: value  2. `y`: count");
	bool found_num1 = false;
	bool found_num2 = false;
	for (const auto &t : num_toks) {
		if (t.text == "1.") {
			found_num1 = true;
			assert(t.color_pair == 18);
			assert((t.attr & A_BOLD) != 0);
		}
		if (t.text == "2.") {
			found_num2 = true;
			assert(t.color_pair == 18);
			assert((t.attr & A_BOLD) != 0);
		}
	}
	assert(found_num1 && found_num2);

	auto link_toks = inspector_window::parse_markdown_line("**Decl:** [src/main.cpp:42](file:///src/main.cpp)");
	bool found_link = false;
	for (const auto &t : link_toks) {
		if (t.text == "src/main.cpp:42") {
			found_link = true;
			assert(t.color_pair == 8);
		}
	}
	assert(found_link);

	std::cout << "Testing set_markdown and rendering..." << std::endl;
	insp.set_markdown("**Title:** `Test`\n1. First  2. Second\n**Decl:** src/test.cpp:1", 85, "custom");
	assert(insp.get_score() == 85);
	assert(insp.get_provider_name() == "custom");
	assert(insp.get_state() == inspector_state::active);
	insp.draw_border();
	insp.draw_content(false);

	endwin();
	std::cout << "inspector_window tests passed successfully!" << std::endl;
	return 0;
}
