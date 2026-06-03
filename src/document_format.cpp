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
#include "highlighter_registry.h"
#include "lsp_manager.h"
#include "project_manager.h"


namespace fs = std::filesystem;

void document::format_range(int start_y, int end_y)
{
	if (is_read_only())
		return;

	std::string temp_path;
	std::string style_arg;

	struct temp_file_guard {
		fs::path dir_path;
		~temp_file_guard() {
			if (!dir_path.empty()) {
				std::error_code ec;
				std::filesystem::remove_all(dir_path, ec);
			}
		}
	} guard;

	{
		std::unique_lock lock(mutex_);
		if (start_y < 0 || end_y >= line_count_unlocked() || start_y > end_y)
			return;

		// Generate a unique temp directory path in system temp directory
		static std::atomic<unsigned int> temp_counter{0};
		fs::path system_tmp = fs::temp_directory_path();
		fs::path format_tmp_dir = system_tmp / std::format("turbostar_format_{}_{}_{}", getpid(), std::this_thread::get_id(), ++temp_counter);
		fs::create_directories(format_tmp_dir);
		guard.dir_path = format_tmp_dir;

		temp_path = (format_tmp_dir / "format_tmp.cpp").string();

		std::ofstream tmp_file(temp_path);
		if (!tmp_file.is_open()) {
			event_logger::get_instance().log("Format failed: Could not create temp file.");
			return;
		}

		for (int i = 0; i < line_count_unlocked(); ++i) {
			if (i == start_y) {
				tmp_file << "// TS_FORMAT_START\n";
			}
			tmp_file << lines_[i]->get_text() << "\n";
			if (i == end_y) {
				tmp_file << "// TS_FORMAT_END\n";
			}
		}
		tmp_file.close();

		// Determine style
		std::string style = config_manager::get_instance().get_clang_format_style();

		// Check if a .clang-format file exists in the project.
		bool force_file = false;
		fs::path search_path;
		if (!filename_.empty() && filename_ != "unknown.txt") {
			search_path = fs_utils::safe_absolute(filename_).parent_path();
		} else {
			search_path = fs::current_path();
		}

		fs::path clang_format_source;
		while (true) {
			if (fs::exists(search_path / ".clang-format")) {
				force_file = true;
				clang_format_source = search_path / ".clang-format";
				break;
			}
			if (!search_path.has_parent_path() || search_path == search_path.parent_path())
				break;
			search_path = search_path.parent_path();
		}

		if (!force_file) {
			std::string proj_root = project_manager::get_instance().get_project_root();
			if (fs::exists(fs::path(proj_root) / ".clang-format")) {
				force_file = true;
				clang_format_source = fs::path(proj_root) / ".clang-format";
			}
		}

		if (force_file && !clang_format_source.empty()) {
			std::error_code ec;
			fs::copy_file(clang_format_source, format_tmp_dir / ".clang-format", fs::copy_options::overwrite_existing, ec);
			if (ec) {
				event_logger::get_instance().log("Copy .clang-format failed: {}", ec.message());
			} else {
				event_logger::get_instance().log("Copy .clang-format succeeded");
			}
		}

		style_arg = "--style=" + (force_file ? "file" : style);
	}

	// Run clang-format
	sync_command_runner runner;
	runner.apply_internal_profile();
	int exit_code = runner.execute("clang-format {} -i {}", style_arg, temp_path);

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

	if (formatted_block.empty()) {
		event_logger::get_instance().log("Format failed: Could not find markers in output.");
		return;
	}

	// Replace the range
	std::unique_lock lock(mutex_);
	// Re-verify bounds
	if (start_y < 0 || end_y >= line_count_unlocked() || start_y > end_y) {
		event_logger::get_instance().log("Format failed: Document structure changed during formatting.");
		return;
	}

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
	update_target_cursor_x_unlocked();
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

void document::trim_trailing_whitespace()
{
	if (is_read_only())
		return;

	std::unique_lock lock(mutex_);
	if (lines_.empty())
		return;

	int start_y = 0;
	int end_y = line_count_unlocked() - 1;

	// If there's an active selection, scope to selection
	if (selection_start_y_ != -1 && selection_end_y_ != -1) {
		int sx, sy, ex, ey;
		get_selection_range_unlocked(sx, sy, ex, ey);
		start_y = sy;
		end_y = ey;
	}

	begin_edit_group("Trim trailing whitespace");

	bool any_modified = false;
	for (int y = start_y; y <= end_y; ++y) {
		std::string orig_text = lines_[y]->get_text();
		
		// Find trailing whitespace (spaces and tabs)
		size_t end_idx = orig_text.find_last_not_of(" \t");
		std::string trimmed;
		if (end_idx == std::string::npos) {
			// Line is entirely whitespace
			trimmed = "";
		} else {
			trimmed = orig_text.substr(0, end_idx + 1);
		}

		if (trimmed.length() < orig_text.length()) {
			record_action(edit_action::action_type::replace_line, y, lines_[y]);
			lines_[y]->set_text(trimmed);
			mark_line_dirty(lines_[y]);
			any_modified = true;
		}
	}

	end_edit_group();

	if (any_modified) {
		set_modified();
		// If cursor_x_ is now past the end of the current line (if it was trimmed), adjust cursor_x_
		int max_x = lines_[cursor_y_]->length_in_chars();
		if (cursor_x_ > max_x) {
			cursor_x_ = max_x;
			update_target_cursor_x_unlocked();
		}
	}

	lock.unlock();
	notify_cursor_changed();
}

