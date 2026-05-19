/**
 * @file document.cpp
 * @brief Core document management and file I/O for Turbostar.
 * 
 * NOTE: Due to its size, the document implementation has been split into logical sub-modules.
 * If you are looking for specific functionality, please check the corresponding file:
 * 
 * - `document_edit.cpp`: Text modification (insert, delete, split, append lines)
 * - `document_format.cpp`: Code formatting (clang-format integration)
 * - `document_highlight.cpp`: Syntax highlighting and background thread processing
 * - `document_nav.cpp`: Cursor movement and viewport navigation
 * - `document_search.cpp`: Text search, matching brackets, and scope selection
 * - `document_selection.cpp`: Selection marking, copying, moving, and deleting blocks
 * - `document_undo.cpp`: Undo/Redo history and action group management
 */

#include "document.h"
#include "history_manager.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include "event_logger.h"
#include "config_manager.h"
#include "git_manager.h"
#include "highlighter_registry.h"
#include "clangd_manager.h"
#include "fs_utils.h"
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

document::document(event_queue &global_queue) : global_queue_(global_queue)
{
	lines_.push_back(std::make_shared<line>(""));
	refresh_highlighter();
	highlighter_thread_ = std::thread(&document::highlighter_thread_loop, this);
	notify_cursor_changed();
}

document::document(event_queue &global_queue, const std::string &filename) : filename_(filename), global_queue_(global_queue)
{
	if (filename.empty() || !load_from_file(filename)) {
		if (lines_.empty())
			lines_.push_back(std::make_shared<line>(""));
	}
	refresh_highlighter();
	highlighter_thread_ = std::thread(&document::highlighter_thread_loop, this);
	notify_cursor_changed();
}

document::~document()
{
	stop_thread_ = true;
	dirty_cv_.notify_all();
	if (highlighter_thread_.joinable()) {
		highlighter_thread_.join();
	}
}

bool document::load_from_file(const std::string &filename)
{
	std::unique_lock lock(mutex_);
	std::ifstream file(filename);
	if (!file.is_open()) {
		event_logger::get_instance().log("Load failed: Could not open file " + filename);
		return false;
	}

	lines_.clear();
	std::string line_text;
	while (std::getline(file, line_text)) {
		auto l = std::make_shared<line>(line_text);
		lines_.push_back(l);
		mark_line_dirty(l);
	}
	if (lines_.empty()) {
		lines_.push_back(std::make_shared<line>(""));
	}

	filename_ = filename;
	refresh_highlighter();
	modified_ = false;
	cursor_x_ = 0;
	cursor_y_ = 0;
	selection_start_x_ = selection_start_y_ = -1;
	selection_end_x_ = selection_end_y_ = -1;

	undo_stack_.clear();
	redo_stack_.clear();
	current_action_group_.actions.clear();
	edit_group_depth_ = 0;

	event_logger::get_instance().log("Document loaded from: " + filename + " (" +
					 std::to_string(line_count_unlocked()) + " lines)");

	auto pos_opt = history_manager::get_instance().get_cursor_pos(filename_);
	if (pos_opt) {
		cursor_x_ = pos_opt->x;
		cursor_y_ = pos_opt->y;
		if (cursor_y_ >= line_count_unlocked()) cursor_y_ = line_count_unlocked() - 1;
		if (cursor_y_ < 0) cursor_y_ = 0;
		if (cursor_x_ > lines_[cursor_y_]->length_in_chars()) cursor_x_ = lines_[cursor_y_]->length_in_chars();
		if (cursor_x_ < 0) cursor_x_ = 0;
		target_cursor_x_ = cursor_x_;
	}

	lock.unlock();
	git_manager::get_instance().request_status(filename);
	clangd_manager::get_instance().open_document(filename, get_text_all());
	notify_cursor_changed();
	return true;
}


void document::insert_file(const std::string &filename)
{
	std::ifstream file(filename);
	if (!file.is_open()) {
		event_logger::get_instance().log("Insert File failed: Could not open file " + filename);
		return;
	}

	std::vector<line> block;
	std::string line_text;
	while (std::getline(file, line_text)) {
		block.emplace_back(line_text);
	}

	if (block.empty())
		return;

	std::unique_lock lock(mutex_);
	begin_edit_group();
	insert_block(block);
	end_edit_group();
	set_modified();
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}


bool document::save()
{
	std::shared_lock lock(mutex_);
	std::string fname = filename_;
	bool modified = modified_;
	lock.unlock();

	if (fname.empty()) {
		event_logger::get_instance().log("Save failed: No filename specified.");
		return false;
	}

	if (!modified) {
		git_manager::get_instance().request_status(fname);
		return true;
	}

	return save_to_file(fname);
}


bool document::save_to_file(const std::string &filename)
{
	std::unique_lock lock(mutex_);

	if (fs::exists(filename)) {
		try {
			std::error_code ec;
			fs::rename(filename, filename + "~", ec);
			if (ec) {
				event_logger::get_instance().log("Backup rename failed: " + ec.message());
				// Fallback to copy if rename fails (e.g., cross-device, though unlikely here)
				fs::copy_file(filename, filename + "~", fs::copy_options::overwrite_existing);
			}
		} catch (const std::exception &e) {
			event_logger::get_instance().log("Backup failed: " + std::string(e.what()));
		}
	}

	std::ofstream file(filename);
	if (!file.is_open()) {
		event_logger::get_instance().log("Save failed: Could not open file " + filename);
		return false;
	}

	for (int i = 0; i < line_count_unlocked(); ++i) {
		file << lines_[i]->get_text();
		if (i < line_count_unlocked() - 1) {
			file << "\n";
		}
	}

	filename_ = filename;
	refresh_highlighter();
	modified_ = false;
	event_logger::get_instance().log("Document saved to: " + filename);
	lock.unlock();
	git_manager::get_instance().request_status(filename);
	clangd_manager::get_instance().update_document(filename, get_text_all());
	notify_cursor_changed();
	return true;
}


void document::clear()
{
	std::unique_lock lock(mutex_);
	lines_.clear();
	auto l = std::make_shared<line>("");
	lines_.push_back(l);
	mark_line_dirty(l);
	filename_ = "";
	refresh_highlighter();
	modified_ = false;
	cursor_x_ = 0;
	cursor_y_ = 0;
	selection_start_x_ = selection_start_y_ = -1;
	selection_end_x_ = selection_end_y_ = -1;
	
	undo_stack_.clear();
	redo_stack_.clear();
	current_action_group_.actions.clear();
	edit_group_depth_ = 0;

	lock.unlock();
	notify_cursor_changed();
}

const std::string &document::get_filename() const
{
	std::shared_lock lock(mutex_);
	return filename_;
}


bool document::has_nondefault_filename() const
{
	std::shared_lock lock(mutex_);
	event_logger::get_instance().log("has_nondefault_filename: current='" + filename_ + "'");
	return !filename_.empty() && filename_ != "unknown.txt";
}


bool document::is_modified() const
{
	std::shared_lock lock(mutex_);
	return modified_;
}


void document::clear_modified()
{
	std::unique_lock lock(mutex_);
	modified_ = false;
	lock.unlock();
	notify_cursor_changed();
}

void document::set_read_only(bool ro)
{
	read_only_ = ro;
	if (ro) {
		clear_modified();
	}
}


std::string document::get_git_branch() const
{
	std::shared_lock lock(mutex_);
	return git_branch_;
}


void document::set_git_branch(const std::string &branch)
{
	std::unique_lock lock(mutex_);
	git_branch_ = branch;
}


int document::line_count() const
{
	std::shared_lock lock(mutex_);
	return line_count_unlocked();
}


size_t document::get_line_count() const
{
	std::shared_lock lock(mutex_);
	return static_cast<size_t>(line_count_unlocked());
}


int document::line_count_unlocked() const
{
	return static_cast<int>(lines_.size());
}


std::shared_ptr<line> document::get_line(int index) const
{
	std::shared_lock lock(mutex_);
	if (index >= 0 && index < line_count_unlocked())
		return lines_[index];
	return nullptr;
}

std::vector<std::string> document::get_all_lines() const
{
	std::shared_lock lock(mutex_);
	std::vector<std::string> result;
	result.reserve(lines_.size());
	for (const auto& l : lines_) {
		result.push_back(l->get_text());
	}
	return result;
}

std::vector<diagnostic_info> document::get_diagnostics() const
{
	std::shared_lock lock(mutex_);
	return lsp_diagnostics_;
}

int document::get_cursor_x() const
{
	std::shared_lock lock(mutex_);
	return cursor_x_;
}


int document::get_cursor_y() const
{
	std::shared_lock lock(mutex_);
	return cursor_y_;
}


std::string document::get_text_all() const
{
	std::shared_lock lock(mutex_);
	std::string full_text;
	for (size_t i = 0; i < lines_.size(); ++i) {
		full_text += lines_[i]->get_text();
		if (i < lines_.size() - 1) {
			full_text += "\n";
		}
	}
	return full_text;
}


std::string document::get_word_under_cursor() const
{
	std::shared_lock lock(mutex_);
	if (cursor_y_ < 0 || cursor_y_ >= static_cast<int>(lines_.size())) return "";
	std::string text = lines_[cursor_y_]->get_text();
	if (text.empty() || cursor_x_ < 0 || cursor_x_ > static_cast<int>(text.length())) return "";

	int start = cursor_x_;
	while (start > 0) {
		char c = text[start - 1];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
			start--;
		} else {
			break;
		}
	}

	int end = cursor_x_;
	while (end < static_cast<int>(text.length())) {
		char c = text[end];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
			end++;
		} else {
			break;
		}
	}

	if (start < end) {
		return text.substr(start, end - start);
	}
	return "";
}

void document::notify_cursor_changed() const
{
	std::string word = get_word_under_cursor();
	if (!filename_.empty() && filename_ != "unknown.txt" && word != last_hover_word_) {
		last_hover_word_ = word;
		if (!word.empty()) {
			clangd_manager::get_instance().request_hover(filename_, cursor_y_, cursor_x_);
			clangd_manager::get_instance().request_document_highlight(filename_, cursor_y_, cursor_x_);
		} else {
			// Clear highlights if we moved off a word
			std::unique_lock lock2(mutex_);
			lsp_highlights_.clear();
			lock2.unlock();
			
			request_redraw();
		}
	}

	std::shared_lock lock(mutex_);
	int cur_disp_x = lines_[cursor_y_]->char_to_display_col(cursor_x_);
	std::string msg = "State: C=" + std::to_string(cursor_y_ + 1) + ":" + std::to_string(cur_disp_x + 1);

	if (selection_start_y_ != -1) {
		int sel_start_disp_x = lines_[selection_start_y_]->char_to_display_col(selection_start_x_);
		msg += " S=" + std::to_string(selection_start_y_ + 1) + ":" + std::to_string(sel_start_disp_x + 1);
	} else {
		msg += " S=none";
	}

	if (selection_end_y_ != -1) {
		int sel_end_disp_x = lines_[selection_end_y_]->char_to_display_col(selection_end_x_);
		msg += " E=" + std::to_string(selection_end_y_ + 1) + ":" + std::to_string(sel_end_disp_x + 1);
	} else {
		msg += " E=none";
	}

	event_logger::get_instance().log(msg);
}


void document::request_redraw() const
{
	editor_event ev;
	ev.type = event_type::redraw;
	global_queue_.push(ev);
}

void document::set_modified()
{
	modified_ = true;
	lsp_diagnostics_.clear();
}


bool document::is_space_at(int y, int x) const
{
	std::shared_lock lock(mutex_);
	return is_space_at_unlocked(y, x);
}


bool document::is_space_at_unlocked(int y, int x) const
{
	std::string text = lines_[y]->get_text();
	size_t offset = lines_[y]->char_to_byte_offset(x);
	if (offset < text.length()) {
		return std::isspace(static_cast<unsigned char>(text[offset]));
	}
	return false;
}

void document::apply_external_edits_json(const std::string& json_str) {
	if (is_read_only()) return;
	try {
		auto j = nlohmann::json::parse(json_str);
		if (!j.is_array()) return;
		
		std::unique_lock lock(mutex_);
		begin_edit_group();
		
		// The edits must be descending to avoid index shifts
		for (const auto& edit : j) {
			if (!edit.contains("line_number") || !edit.contains("type")) continue;
			
			int idx = edit["line_number"].get<int>() - 1;
			std::string type = edit["type"].get<std::string>();
			
			if (idx < 0 || idx >= static_cast<int>(lines_.size())) continue;
			
			if (type == "remove") {
				cursor_y_ = idx; cursor_x_ = 0;
				record_action(edit_action::action_type::delete_line, idx, lines_[idx]);
				lines_.erase(lines_.begin() + idx);
			} else if (type == "add" && edit.contains("replace_with")) {
				std::string newstring = edit["replace_with"].get<std::string>();
				std::stringstream ss(newstring);
				std::string part;
				std::vector<std::shared_ptr<line>> new_lines;
				if (newstring.empty()) {
					new_lines.push_back(std::make_shared<line>(""));
				} else {
					while (std::getline(ss, part)) {
						if (!part.empty() && part.back() == '\r') part.pop_back();
						new_lines.push_back(std::make_shared<line>(part));
					}
				}
				
				cursor_y_ = idx; cursor_x_ = 0;
				for (size_t i = 0; i < new_lines.size(); ++i) {
					lines_.insert(lines_.begin() + idx + i, new_lines[i]);
					record_action(edit_action::action_type::insert_line, idx + i, nullptr);
					mark_line_dirty(new_lines[i]);
				}
			} else if (type == "replace" && edit.contains("replace_with")) {
				cursor_y_ = idx; cursor_x_ = 0;
				record_action(edit_action::action_type::delete_line, idx, lines_[idx]);
				lines_.erase(lines_.begin() + idx);
				
				std::string newstring = edit["replace_with"].get<std::string>();
				std::stringstream ss(newstring);
				std::string part;
				std::vector<std::shared_ptr<line>> new_lines;
				if (newstring.empty()) {
					new_lines.push_back(std::make_shared<line>(""));
				} else {
					while (std::getline(ss, part)) {
						if (!part.empty() && part.back() == '\r') part.pop_back();
						new_lines.push_back(std::make_shared<line>(part));
					}
				}
				
				for (size_t i = 0; i < new_lines.size(); ++i) {
					lines_.insert(lines_.begin() + idx + i, new_lines[i]);
					record_action(edit_action::action_type::insert_line, idx + i, nullptr);
					mark_line_dirty(new_lines[i]);
				}
			}
		}
		
		end_edit_group();
		set_modified();
		lock.unlock();
		notify_cursor_changed();
		request_redraw();
	} catch (...) {
		event_logger::get_instance().log("Failed to parse or apply external edits json");
	}
}
