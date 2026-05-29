#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
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
		nlohmann::json remove_edits = nlohmann::json::array({
			{{"line_number", 6}, {"type", "remove"}, {"lines_to_remove", 2}}
		});
		doc.apply_external_edits_json(remove_edits.dump());
		assert(doc.line_count() == 8);
		assert(doc.get_cursor_y() == 4);
		assert(doc.get_cursor_x() == 2);

		// Now remove lines 3 and 4 (0-indexed 2 and 3)
		// Since y=4 is after the edit (edit_idx=2, lines_to_remove=2),
		// it should shift y by -2 to become y=2.
		nlohmann::json remove_edits2 = nlohmann::json::array({
			{{"line_number", 3}, {"type", "remove"}, {"lines_to_remove", 2}}
		});
		doc.apply_external_edits_json(remove_edits2.dump());
		assert(doc.line_count() == 6);
		assert(doc.get_cursor_y() == 2);
		assert(doc.get_cursor_x() == 2);

		// Now remove line 3 (0-indexed 2, which is the cursor line)
		// y=2 is inside the deleted block. It should snap to y=2, x=0.
		nlohmann::json remove_cursor_line = nlohmann::json::array({
			{{"line_number", 3}, {"type", "remove"}, {"lines_to_remove", 1}}
		});
		doc.apply_external_edits_json(remove_cursor_line.dump());
		assert(doc.line_count() == 5);
		assert(doc.get_cursor_y() == 2);
		assert(doc.get_cursor_x() == 0);
	}

	std::cout << "document unit test passed!\n";
	return 0;
}
