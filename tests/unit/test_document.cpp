#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include "../../src/document.h"
#include "../../src/event_queue.h"

int main()
{
	event_queue queue;

	// Test 1: insert_file sets modified flag
	{
		document doc(queue);
		doc.load_from_file("tests/unit/poem.txt");
		assert(!doc.is_modified());

		std::string temp_file = "temp_insert.txt";
		{
			std::ofstream ofs(temp_file);
			ofs << "Inserted Line 1\nInserted Line 2\n";
		}

		bool success = doc.insert_file(temp_file);
		assert(success);
		assert(doc.is_modified());
		std::remove(temp_file.c_str());
	}

	// Test 2: apply_external_edits_json cursor adjustment logic
	{
		document doc(queue);
		std::string temp_file = "temp_edits.txt";
		{
			std::ofstream ofs(temp_file);
			for (int i = 1; i <= 10; ++i) {
				ofs << "Line " << i << "\n";
			}
		}
		doc.load_from_file(temp_file);
		std::remove(temp_file.c_str());

		assert(doc.line_count() == 10);

		// Set cursor to line 5 (0-indexed 4), col 2 (currently at 0,0)
		doc.move_cursor(2, 4);
		assert(doc.get_cursor_y() == 4);
		assert(doc.get_cursor_x() == 2);

		// Remove lines 6 and 7 (0-indexed 5 and 6)
		// This should not affect cursor at index 4
		nlohmann::json remove_edits = nlohmann::json::array({{{"line_number", 6}, {"type", "remove"}, {"lines_to_remove", 2}}});
		doc.apply_external_edits_json(remove_edits.dump());
		assert(doc.line_count() == 8);
		assert(doc.get_cursor_y() == 4);
		assert(doc.get_cursor_x() == 2);

		// Now remove lines 3 and 4 (0-indexed 2 and 3)
		// Since y=4 is after the edit (edit_idx=2, lines_to_remove=2),
		// it should shift y by -2 to become y=2.
		nlohmann::json remove_edits2 = nlohmann::json::array({{{"line_number", 3}, {"type", "remove"}, {"lines_to_remove", 2}}});
		doc.apply_external_edits_json(remove_edits2.dump());
		assert(doc.line_count() == 6);
		assert(doc.get_cursor_y() == 2);
		assert(doc.get_cursor_x() == 2);

		// Now remove line 3 (0-indexed 2, which is the cursor line)
		// y=2 is inside the deleted block. It should snap to y=2, x=0.
		nlohmann::json remove_cursor_line =
		    nlohmann::json::array({{{"line_number", 3}, {"type", "remove"}, {"lines_to_remove", 1}}});
		doc.apply_external_edits_json(remove_cursor_line.dump());
		assert(doc.line_count() == 5);
		assert(doc.get_cursor_y() == 2);
		assert(doc.get_cursor_x() == 0);
		// Test 3: apply_external_edits_json with unsorted/ascending edits
		document doc3(queue);
		std::string temp_file3 = "temp_edits3.txt";
		{
			std::ofstream ofs(temp_file3);
			for (int i = 1; i <= 10; ++i) {
				ofs << "Line " << i << "\n";
			}
		}
		doc3.load_from_file(temp_file3);
		std::remove(temp_file3.c_str());

		// Remove line 3 and then line 6 (ascending order)
		nlohmann::json ascending_edits = nlohmann::json::array({{{"line_number", 3}, {"type", "remove"}, {"lines_to_remove", 1}},
									{{"line_number", 6}, {"type", "remove"}, {"lines_to_remove", 1}}});
		doc3.apply_external_edits_json(ascending_edits.dump());

		// If sorted correctly, the final document should be:
		// Line 1, 2, 4, 5, 7, 8, 9, 10
		assert(doc3.line_count() == 8);
		assert(doc3.get_line(2)->get_text() == "Line 4");
		assert(doc3.get_line(4)->get_text() == "Line 7");

		// Test 15-line file, edit line 15, add at line 16
		document doc15(queue);
		std::string temp_file15 = "temp_15_lines.txt";
		{
			std::ofstream ofs(temp_file15);
			for (int i = 1; i <= 15; ++i) {
				ofs << "Line " << i << "\n";
			}
		}
		doc15.load_from_file(temp_file15);
		std::remove(temp_file15.c_str());
		assert(doc15.line_count() == 15);

		// Edit line 15 (replace)
		nlohmann::json edit15 =
		    nlohmann::json::array({{{"line_number", 15}, {"type", "replace"}, {"replace_with", "Replaced Line 15"}}});
		doc15.apply_external_edits_json(edit15.dump());
		assert(doc15.line_count() == 15);
		assert(doc15.get_line(14)->get_text() == "Replaced Line 15");

		// Add at line 16 (append at end of 15 lines)
		nlohmann::json add16 = nlohmann::json::array({{{"line_number", 16}, {"type", "add"}, {"replace_with", "Line 16"}}});
		doc15.apply_external_edits_json(add16.dump());
		assert(doc15.line_count() == 16);
		assert(doc15.get_line(15)->get_text() == "Line 16");
	}

	// Test 4: Undo Coalescing (Typing)
	{
		document doc(queue);
		doc.append_line("Original");
		doc.move_cursor(0, 0); // Put cursor at line 0, x=0

		// Clear whatever was recorded for append_line / cursor move
		doc.break_undo_coalescing();
		size_t initial_undos = doc.get_undo_count();

		// Type "hello" character by character
		doc.insert_char("h");
		doc.insert_char("e");
		doc.insert_char("l");
		doc.insert_char("l");
		doc.insert_char("o");

		// Without coalescing, this is 5 undos. With coalescing, it must be 1.
		assert(doc.get_undo_count() - initial_undos == 1);

		// Undo once
		doc.undo();
		assert(doc.get_line(0)->get_text() == "Original");
	}

	// Test 5: Trim Trailing Whitespace
	{
		document doc(queue);
		doc.append_line("Line 1  ");
		doc.append_line("Line 2\t ");
		doc.append_line("Line 3");
		doc.append_line("   "); // Entirely spaces

		doc.break_undo_coalescing();

		// Trim the whole document
		doc.trim_trailing_whitespace();

		assert(doc.get_line(0)->get_text() == "Line 1");
		assert(doc.get_line(1)->get_text() == "Line 2");
		assert(doc.get_line(2)->get_text() == "Line 3");
		assert(doc.get_line(3)->get_text() == "");

		// Test undo
		doc.undo();
		assert(doc.get_line(0)->get_text() == "Line 1  ");
		assert(doc.get_line(1)->get_text() == "Line 2\t ");
		assert(doc.get_line(3)->get_text() == "   ");

		// Test selection scope
		doc.set_selection(1, 0, 2, 6); // Selection covers lines 1 and 2
		doc.trim_trailing_whitespace();

		// Line 0 (outside selection) should not be trimmed.
		// Line 1 (inside selection) should be trimmed.
		// Line 3 (outside selection) should not be trimmed.
		assert(doc.get_line(0)->get_text() == "Line 1  ");
		assert(doc.get_line(1)->get_text() == "Line 2");
		assert(doc.get_line(3)->get_text() == "   ");
	}

	// Test 6: Check disk change detection
	{
		document doc(queue);
		std::string temp_file = "temp_disk_change.txt";
		{
			std::ofstream ofs(temp_file);
			ofs << "Initial content\n";
		}

		// Load document
		bool loaded = doc.load_from_file(temp_file);
		assert(loaded);
		assert(!doc.check_disk_changed());

		// Wait slightly to ensure different modification time resolution
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		// Modify file externally on disk
		{
			std::ofstream ofs(temp_file);
			ofs << "External content\n";
		}

		// It should detect the change
		assert(doc.check_disk_changed());

		// Ignore changes
		doc.set_ignore_disk_changes(true);
		assert(doc.get_ignore_disk_changes());

		// It should no longer report changed
		assert(!doc.check_disk_changed());

		// Turn ignore off
		doc.set_ignore_disk_changes(false);
		assert(!doc.get_ignore_disk_changes());
		assert(doc.check_disk_changed());

		// Update the last write time in document
		doc.update_last_disk_mtime();

		// It should no longer report changed
		assert(!doc.check_disk_changed());

		std::remove(temp_file.c_str());
	}

	// Test 7: Tab-aware Ghost X vertical navigation
	{
		document doc(queue);
		std::string temp_file = "temp_ghost_x_tabs.txt";
		{
			std::ofstream ofs(temp_file);
			ofs << "\t\tfoo\n";
			ofs << "bar\n";
			ofs << "\t\tbar\n";
		}
		doc.load_from_file(temp_file);
		std::remove(temp_file.c_str());

		assert(doc.line_count() == 3);
		assert(doc.get_cursor_y() == 0);
		assert(doc.get_cursor_x() == 0);

		// Move to the end of the first line: "\t\tfoo" (2 tabs + 3 letters = 5 characters)
		doc.move_cursor(5, 0);
		assert(doc.get_cursor_y() == 0);
		assert(doc.get_cursor_x() == 5);

		// Move down to Line 1 ("bar", length 3)
		doc.move_cursor(0, 1);
		assert(doc.get_cursor_y() == 1);
		// Since Line 1 only has 3 characters, cursor_x should clamp to 3
		assert(doc.get_cursor_x() == 3);

		// Move down to Line 2 ("\t\tbar")
		doc.move_cursor(0, 1);
		assert(doc.get_cursor_y() == 2);
		// Visual column target (19) should be restored.
		// "\t\tbar" has 2 tabs + 3 letters = 5 characters.
		// So cursor_x should be restored to 5.
		assert(doc.get_cursor_x() == 5);

		// Go back to the top
		doc.move_cursor(0, -2);
		assert(doc.get_cursor_y() == 0);
		assert(doc.get_cursor_x() == 5);

		// Test move_page_down(1) to line 1
		doc.move_page_down(1);
		assert(doc.get_cursor_y() == 1);
		assert(doc.get_cursor_x() == 3);

		// Test move_page_down(1) to line 2
		doc.move_page_down(1);
		assert(doc.get_cursor_y() == 2);
		assert(doc.get_cursor_x() == 5);

		// Test move_page_up(1) back to line 1
		doc.move_page_up(1);
		assert(doc.get_cursor_y() == 1);
		assert(doc.get_cursor_x() == 3);

		// Test move_page_up(1) back to line 0
		doc.move_page_up(1);
		assert(doc.get_cursor_y() == 0);
		assert(doc.get_cursor_x() == 5);
	}

	// Test 8: Structural Whole-Line Selection Deletion and Move
	{
		document doc(queue);
		doc.append_line("Line 1");
		doc.append_line("Line 2");
		doc.append_line("Line 3");
		doc.append_line("Line 4");

		// The document initially has 4 lines (the first empty line was replaced by Line 1)
		assert(doc.get_line_count() == 4);
		assert(doc.get_line(0)->get_text() == "Line 1");
		assert(doc.get_line(1)->get_text() == "Line 2");
		assert(doc.get_line(2)->get_text() == "Line 3");
		assert(doc.get_line(3)->get_text() == "Line 4");

		// Select Line 2 completely (starts at line 2 index 1 column 0, ends at line 2 index 1 column 6)
		doc.set_selection(1, 0, 1, 6);
		doc.break_undo_coalescing();

		// Delete selection
		doc.delete_selection();

		// Under the new logic: Line 2 should be structurally deleted!
		// The document should have 3 lines now, and the content of Line 2 should be completely gone.
		assert(doc.get_line_count() == 3);
		assert(doc.get_line(0)->get_text() == "Line 1");
		assert(doc.get_line(1)->get_text() == "Line 3");
		assert(doc.get_line(2)->get_text() == "Line 4");

		// Test undo
		doc.undo();
		assert(doc.get_line_count() == 4);
		assert(doc.get_line(1)->get_text() == "Line 2");
	}

	// Test 9: Move whole line followed by a blank line (prevent eating blank line)
	{
		document doc(queue);
		doc.append_line("Line 1");
		doc.append_line("Line 2");
		doc.append_line(""); // Blank line
		doc.append_line("Line 3");

		// Document:
		// index 0: Line 1
		// index 1: Line 2
		// index 2:
		// index 3: Line 3

		assert(doc.get_line_count() == 4);
		assert(doc.get_line(1)->get_text() == "Line 2");
		assert(doc.get_line(2)->get_text() == "");

		// Select Line 2 (index 1) completely
		doc.set_selection(1, 0, 1, 6);
		doc.break_undo_coalescing();

		// Move cursor to the blank line (index 2)
		doc.move_cursor(0, -1); // cursor_y becomes 2
		assert(doc.get_cursor_y() == 2);

		// Move selection (should be a no-op structurally, but blank line must not be eaten)
		doc.move_selection();

		assert(doc.get_line_count() == 4);
		assert(doc.get_line(0)->get_text() == "Line 1");
		assert(doc.get_line(1)->get_text() == "Line 2");
		assert(doc.get_line(2)->get_text() == "");
		assert(doc.get_line(3)->get_text() == "Line 3");

		// Now let's try moving it below the blank line.
		// Select Line 2 (index 1) again
		doc.set_selection(1, 0, 1, 6);
		doc.break_undo_coalescing();

		// Move cursor to Line 3 (index 3)
		doc.move_cursor(0, 2); // cursor_y becomes 3
		assert(doc.get_cursor_y() == 3);

		doc.move_selection();

		// Expected output: Line 2 is moved below the blank line (goes to index 2, blank line at index 1)
		assert(doc.get_line_count() == 4);
		assert(doc.get_line(0)->get_text() == "Line 1");
		assert(doc.get_line(1)->get_text() == "");
		assert(doc.get_line(2)->get_text() == "Line 2");
		assert(doc.get_line(3)->get_text() == "Line 3");
	}

	// Test 10: write_selection_to_file
	{
		document doc(queue);
		doc.append_line("Line 1");
		doc.append_line("Line 2");
		doc.append_line("Line 3");
		doc.append_line("Line 4");

		// Select Line 2 and Line 3
		// Selection: Line 2 start to Line 3 end
		doc.set_selection(1, 0, 2, 6);

		std::string temp_file = "temp_selection_write.txt";
		std::filesystem::remove(temp_file);

		bool success = doc.write_selection_to_file(temp_file);
		assert(success);

		// Read content of temp_file and verify it contains Line 2 and Line 3 only
		std::ifstream ifs(temp_file);
		std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		assert(content == "Line 2\nLine 3");

		std::filesystem::remove(temp_file);
	}

	std::cout << "document unit test passed!\n";
	return 0;
}
