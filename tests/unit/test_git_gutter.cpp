// Tested source file: src/git_manager.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <memory>
#include "git_manager.h"
#include "event_queue.h"
#include "ui/window.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	std::cout << "Starting test_git_gutter..." << std::endl;

	// 1. Test empty git_file_diff
	{
		git_file_diff diff;
		assert(diff.empty());
		auto info1 = diff.get_line_info(1);
		assert(info1.type == git_line_diff_type::unchanged);
		assert(info1.git_line == 1);
		assert(!info1.has_deletion_above);

		auto info50 = diff.get_line_info(50);
		assert(info50.type == git_line_diff_type::unchanged);
		assert(info50.git_line == 50);
		assert(!info50.has_deletion_above);
	}

	// 2. Test pure addition hunk: @@ -4,0 +5,3 @@
	// In buffer lines: 1..4 unchanged, 5..7 added, 8+ unchanged
	{
		git_file_diff diff;
		diff.hunks.push_back({4, 0, 5, 3});
		assert(!diff.empty());

		auto info4 = diff.get_line_info(4);
		assert(info4.type == git_line_diff_type::unchanged);
		assert(info4.git_line == 4);
		assert(!info4.has_deletion_above);

		auto info5 = diff.get_line_info(5);
		assert(info5.type == git_line_diff_type::added);
		assert(info5.git_line == -1);
		assert(!info5.has_deletion_above);

		auto info6 = diff.get_line_info(6);
		assert(info6.type == git_line_diff_type::added);
		assert(info6.git_line == -1);

		auto info7 = diff.get_line_info(7);
		assert(info7.type == git_line_diff_type::added);
		assert(info7.git_line == -1);

		auto info8 = diff.get_line_info(8);
		assert(info8.type == git_line_diff_type::unchanged);
		assert(info8.git_line == 5); // 8 - 3 added = 5
		assert(!info8.has_deletion_above);
	}

	// 3. Test pure deletion hunk: @@ -10,2 +9,0 @@
	// Lines 10 and 11 from old were deleted after new line 9.
	// Buffer line 9: unchanged, git_line 9
	// Buffer line 10: unchanged, git_line 12 (gap: 9 -> 12), has_deletion_above = true!
	{
		git_file_diff diff;
		diff.hunks.push_back({10, 2, 9, 0});

		auto info9 = diff.get_line_info(9);
		assert(info9.type == git_line_diff_type::unchanged);
		assert(info9.git_line == 9);
		assert(!info9.has_deletion_above);

		auto info10 = diff.get_line_info(10);
		assert(info10.type == git_line_diff_type::unchanged);
		assert(info10.git_line == 12); // gap of 2 lines!
		assert(info10.has_deletion_above);

		auto info11 = diff.get_line_info(11);
		assert(info11.type == git_line_diff_type::unchanged);
		assert(info11.git_line == 13);
		assert(!info11.has_deletion_above);
	}

	// 4. Test modification hunk: @@ -20,2 +20,2 @@
	// Buffer lines 20 and 21 are modified, git_line 20 and 21
	{
		git_file_diff diff;
		diff.hunks.push_back({20, 2, 20, 2});

		auto info20 = diff.get_line_info(20);
		assert(info20.type == git_line_diff_type::modified);
		assert(info20.git_line == 20);
		assert(!info20.has_deletion_above);

		auto info21 = diff.get_line_info(21);
		assert(info21.type == git_line_diff_type::modified);
		assert(info21.git_line == 21);

		auto info22 = diff.get_line_info(22);
		assert(info22.type == git_line_diff_type::unchanged);
		assert(info22.git_line == 22);
		assert(!info22.has_deletion_above);
	}

	// 5. Test modification + deletion hunk: @@ -30,3 +30,1 @@
	// 1 line modified (line 30), 2 lines deleted above line 31
	{
		git_file_diff diff;
		diff.hunks.push_back({30, 3, 30, 1});

		auto info30 = diff.get_line_info(30);
		assert(info30.type == git_line_diff_type::modified);
		assert(info30.git_line == 30);

		auto info31 = diff.get_line_info(31);
		assert(info31.type == git_line_diff_type::unchanged);
		assert(info31.git_line == 33);
		assert(info31.has_deletion_above);
	}

	// 6. Test modification + addition hunk: @@ -40,1 +40,3 @@
	// Line 40 modified, lines 41..42 added
	{
		git_file_diff diff;
		diff.hunks.push_back({40, 1, 40, 3});

		auto info40 = diff.get_line_info(40);
		assert(info40.type == git_line_diff_type::modified);
		assert(info40.git_line == 40);

		auto info41 = diff.get_line_info(41);
		assert(info41.type == git_line_diff_type::added);
		assert(info41.git_line == -1);

		auto info42 = diff.get_line_info(42);
		assert(info42.type == git_line_diff_type::added);
		assert(info42.git_line == -1);

		auto info43 = diff.get_line_info(43);
		assert(info43.type == git_line_diff_type::unchanged);
		assert(info43.git_line == 41);
	}

	// 7. Test deletion at line 1 (start of file): @@ -1,2 +0,0 @@
	{
		git_file_diff diff;
		diff.hunks.push_back({1, 2, 0, 0});

		auto info1 = diff.get_line_info(1);
		assert(info1.type == git_line_diff_type::unchanged);
		assert(info1.git_line == 3);
		assert(info1.has_deletion_above);
	}

	// 8. Test equality operator of git_file_diff
	{
		git_file_diff d1, d2;
		assert(d1 == d2);
		d1.hunks.push_back({1, 1, 1, 1});
		assert(d1 != d2);
		d2.hunks.push_back({1, 1, 1, 1});
		assert(d1 == d2);
	}

	// 9. Test window gutter mode cycling
	{
		window win(1, 0, 0, 80, 24, "test.txt");
		assert(win.get_gutter_number_mode() == gutter_number_mode::git);

		win.cycle_gutter_number_mode();
		assert(win.get_gutter_number_mode() == gutter_number_mode::file);

		win.cycle_gutter_number_mode();
		assert(win.get_gutter_number_mode() == gutter_number_mode::none);

		win.cycle_gutter_number_mode();
		assert(win.get_gutter_number_mode() == gutter_number_mode::git);

		win.set_gutter_number_mode(gutter_number_mode::file);
		assert(win.get_gutter_number_mode() == gutter_number_mode::file);
	}

	// 10. Multi-hunk file scenario:
	// Hunk 1: @@ -5,0 +5,2 @@ (lines 5,6 added)
	// Hunk 2: @@ -10,3 +12,1 @@ (line 12 modified, lines 11, 12 in old deleted -> deletion above 13)
	{
		git_file_diff diff;
		diff.hunks.push_back({5, 0, 5, 2});
		diff.hunks.push_back({10, 3, 12, 1});

		// Before Hunk 1
		auto l4 = diff.get_line_info(4);
		assert(l4.type == git_line_diff_type::unchanged && l4.git_line == 4);

		// Inside Hunk 1
		auto l5 = diff.get_line_info(5);
		assert(l5.type == git_line_diff_type::added && l5.git_line == -1);
		auto l6 = diff.get_line_info(6);
		assert(l6.type == git_line_diff_type::added && l6.git_line == -1);

		// Between Hunk 1 and Hunk 2:
		// delta after Hunk 1 is +2
		auto l7 = diff.get_line_info(7);
		assert(l7.type == git_line_diff_type::unchanged && l7.git_line == 5);
		auto l11 = diff.get_line_info(11);
		assert(l11.type == git_line_diff_type::unchanged && l11.git_line == 9);

		// Inside Hunk 2
		auto l12 = diff.get_line_info(12);
		assert(l12.type == git_line_diff_type::modified && l12.git_line == 10);

		// After Hunk 2 (deletion above line 13)
		// old had 3 lines (10, 11, 12), new had 1 line (12).
		// So old lines 11, 12 were deleted.
		// Next line 13 corresponds to old line 13.
		auto l13 = diff.get_line_info(13);
		assert(l13.type == git_line_diff_type::unchanged && l13.git_line == 13);
		assert(l13.has_deletion_above);
	}

	std::cout << "All test_git_gutter assertions passed!" << std::endl;
	return 0;
}
