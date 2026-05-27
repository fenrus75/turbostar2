#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include "command_runner.h"
#include "config_manager.h"
#include "document.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "git_manager.h"
#include "highlighter_registry.h"
#include "lsp_manager.h"

namespace fs = std::filesystem;

void document::format_range(int start_y, int end_y)
{
	if (is_read_only())
		return;
	if (start_y < 0 || end_y >= line_count_unlocked() || start_y > end_y)
		return;

	std::string temp_path = "format_tmp.cpp";
	std::ofstream tmp_file(temp_path);
	if (!tmp_file.is_open()) {
		event_logger::get_instance().log("Format failed: Could not create temp file.");
		return;
	}

	{
		std::shared_lock lock(mutex_);
		for (int i = 0; i < line_count_unlocked(); ++i) {
			if (i == start_y) {
				tmp_file << "// TS_FORMAT_START\n";
			}
			tmp_file << lines_[i]->get_text() << "\n";
			if (i == end_y) {
				tmp_file << "// TS_FORMAT_END\n";
			}
		}
	}
	tmp_file.close();

	// Determine style
	std::string style = config_manager::get_instance().get_clang_format_style();

	// Check if a .clang-format file exists in the project.
	// Policy: If a .clang-format file exists in the file's directory or any parent, it always wins.
	bool force_file = false;
	fs::path search_path;
	if (!filename_.empty() && filename_ != "unknown.txt") {
		search_path = fs_utils::safe_absolute(filename_).parent_path();
	} else {
		search_path = fs::current_path();
	}

	while (true) {
		if (fs::exists(search_path / ".clang-format")) {
			force_file = true;
			break;
		}
		if (!search_path.has_parent_path() || search_path == search_path.parent_path())
			break;
		search_path = search_path.parent_path();
	}

	std::string style_arg = "--style=" + (force_file ? "file" : style);

	// Run clang-format
	std::string cmd = "clang-format " + style_arg + " -i " + temp_path;

	sync_command_runner runner;
	runner.apply_internal_profile();
	int exit_code = runner.execute(cmd);

	if (exit_code != 0) {
		event_logger::get_instance().log("Format failed: clang-format returned error.");
		return;
	}

	// Read back and extract
	std::ifstream result_file(temp_path);
	if (!result_file.is_open()) {
		return;
	}

	std::vector<line> formatted_block;
	std::string line_text;
	bool inside_markers = false;
	while (std::getline(result_file, line_text)) {
		if (line_text.find("// TS_FORMAT_START") != std::string::npos) {
			inside_markers = true;
			continue;
		}
		if (line_text.find("// TS_FORMAT_END") != std::string::npos) {
			inside_markers = false;
			break;
		}
		if (inside_markers) {
			formatted_block.emplace_back(line_text);
		}
	}
	result_file.close();
	fs::remove(temp_path);

	if (formatted_block.empty()) {
		event_logger::get_instance().log("Format failed: Could not find markers in output.");
		return;
	}

	// Replace the range
	std::unique_lock lock(mutex_);
	begin_edit_group("Format code");

	// 1. Insert new lines
	for (size_t i = 0; i < formatted_block.size(); ++i) {
		auto nl = std::make_shared<line>(formatted_block[i]);
		lines_.insert(lines_.begin() + start_y + i, nl);
		record_action(edit_action::action_type::insert_line, start_y + static_cast<int>(i), nullptr);
		mark_line_dirty(nl);
	}

	// 2. Delete old lines (now shifted forward by formatted_block.size())
	int old_start = start_y + static_cast<int>(formatted_block.size());
	int num_to_delete = end_y - start_y + 1;
	for (int i = 0; i < num_to_delete; ++i) {
		record_action(edit_action::action_type::delete_line, old_start, lines_[old_start]);
		lines_.erase(lines_.begin() + old_start);
	}

	cursor_y_ = start_y;
	cursor_x_ = 0;

	end_edit_group();
	set_modified();
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::format_paragraph()
{
	if (is_read_only())
		return;
	std::unique_lock lock(mutex_);
	if (lines_.empty())
		return;

	auto is_empty = [&](int y) {
		std::string t = lines_[y]->get_text();
		return std::all_of(t.begin(), t.end(), [](unsigned char ch) { return std::isspace(ch); });
	};

	int sy = cursor_y_;
	int ey = cursor_y_;

	// If we are on an empty line, do nothing or just that line (which is a no-op)
	if (is_empty(cursor_y_)) {
		lock.unlock();
		return;
	}

	while (sy > 0 && !is_empty(sy - 1)) {
		sy--;
	}
	while (ey < line_count_unlocked() - 1 && !is_empty(ey + 1)) {
		ey++;
	}
	lock.unlock();

	format_range(sy, ey);
}
