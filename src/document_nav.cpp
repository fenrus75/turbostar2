#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include "config_manager.h"
#include "document.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "highlighter/highlighter_registry.h"
#include "lsp_manager.h"

namespace fs = std::filesystem;

void document::move_cursor(int dx, int dy)
{
	std::unique_lock lock(mutex_);
	break_undo_coalescing_unlocked();

	if (dy != 0) {
		cursor_y_ += dy;
		if (cursor_y_ < 0)
			cursor_y_ = 0;
		if (cursor_y_ >= line_count_unlocked())
			cursor_y_ = line_count_unlocked() - 1;
		cursor_x_ = lines_[cursor_y_]->display_col_to_char_pos(target_cursor_x_);
	}

	if (dx != 0) {
		if (dx < 0 && cursor_x_ == 0 && cursor_y_ > 0) {
			cursor_y_--;
			cursor_x_ = lines_[cursor_y_]->length_in_chars();
		} else if (dx > 0 && cursor_x_ >= lines_[cursor_y_]->length_in_chars() && cursor_y_ < line_count_unlocked() - 1) {
			cursor_y_++;
			cursor_x_ = 0;
		} else {
			cursor_x_ += dx;
		}
		update_target_cursor_x_unlocked();
	}

	int line_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ < 0)
		cursor_x_ = 0;
	if (cursor_x_ > line_len)
		cursor_x_ = line_len;

	lock.unlock();
	notify_cursor_changed();
}

void document::set_cursor_position(int col, int line)
{
	std::unique_lock lock(mutex_);
	break_undo_coalescing_unlocked();

	cursor_y_ = line;
	if (cursor_y_ < 0)
		cursor_y_ = 0;
	if (cursor_y_ >= line_count_unlocked())
		cursor_y_ = line_count_unlocked() - 1;

	cursor_x_ = col;
	int line_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ < 0)
		cursor_x_ = 0;
	if (cursor_x_ > line_len)
		cursor_x_ = line_len;

	update_target_cursor_x_unlocked();

	lock.unlock();
	notify_cursor_changed();
}

void document::move_to_bol()
{
	std::unique_lock lock(mutex_);
	break_undo_coalescing_unlocked();
	cursor_x_ = 0;
	update_target_cursor_x_unlocked();
	lock.unlock();
	notify_cursor_changed();
}

void document::move_to_eol()
{
	std::unique_lock lock(mutex_);
	break_undo_coalescing_unlocked();
	if (cursor_y_ >= 0 && cursor_y_ < line_count_unlocked()) {
		cursor_x_ = lines_[cursor_y_]->length_in_chars();
	}
	update_target_cursor_x_unlocked();
	lock.unlock();
	notify_cursor_changed();
}

void document::move_to_top()
{
	std::unique_lock lock(mutex_);
	break_undo_coalescing_unlocked();
	cursor_x_ = 0;
	cursor_y_ = 0;
	update_target_cursor_x_unlocked();
	lock.unlock();
	notify_cursor_changed();
}

void document::move_to_bottom()
{
	std::unique_lock lock(mutex_);
	break_undo_coalescing_unlocked();
	cursor_y_ = line_count_unlocked() - 1;
	cursor_x_ = lines_[cursor_y_]->length_in_chars();
	update_target_cursor_x_unlocked();
	lock.unlock();
	notify_cursor_changed();
}

void document::move_page_up(int page_height)
{
	std::unique_lock lock(mutex_);
	break_undo_coalescing_unlocked();
	cursor_y_ -= page_height;
	cursor_y_ = std::max(0, cursor_y_);
	cursor_x_ = lines_[cursor_y_]->display_col_to_char_pos(target_cursor_x_);

	int line_char_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ > line_char_len)
		cursor_x_ = line_char_len;
	lock.unlock();
	notify_cursor_changed();
}

void document::move_page_down(int page_height)
{
	std::unique_lock lock(mutex_);
	break_undo_coalescing_unlocked();
	cursor_y_ += page_height;
	if (cursor_y_ >= line_count_unlocked()) {
		cursor_y_ = line_count_unlocked() - 1;
	}
	cursor_x_ = lines_[cursor_y_]->display_col_to_char_pos(target_cursor_x_);

	int line_char_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ > line_char_len)
		cursor_x_ = line_char_len;
	lock.unlock();
	notify_cursor_changed();
}

void document::move_next_word()
{
	std::unique_lock lock(mutex_);
	break_undo_coalescing_unlocked();
	auto &cur_line = *lines_[cursor_y_];
	int line_char_len = cur_line.length_in_chars();
	int i = cursor_x_;

	if (cur_line.is_terminator_at(i)) {
		i++;
	} else if (auto span = cur_line.get_word_span_at(i); !span.empty()) {
		i = span.end_char_pos;
	} else if (auto ws = cur_line.get_whitespace_span_at(i); !ws.empty()) {
		i = ws.end_char_pos;
	} else if (i < line_char_len) {
		i++;
	}
	if (auto trailing_ws = cur_line.get_whitespace_span_at(i); !trailing_ws.empty()) {
		i = trailing_ws.end_char_pos;
	}

	if (i >= line_char_len && cursor_y_ < line_count_unlocked() - 1) {
		cursor_y_++;
		cursor_x_ = 0;
	} else {
		cursor_x_ = std::min(i, line_char_len);
	}

	update_target_cursor_x_unlocked();
	lock.unlock();
	notify_cursor_changed();
}

void document::move_prev_word()
{
	std::unique_lock lock(mutex_);
	break_undo_coalescing_unlocked();
	if (cursor_x_ == 0) {
		if (cursor_y_ > 0) {
			cursor_y_--;
			cursor_x_ = lines_[cursor_y_]->length_in_chars();
		}
		update_target_cursor_x_unlocked();
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	auto &cur_line = *lines_[cursor_y_];
	int i = cursor_x_;

	// 1. Consume any whitespace directly preceding the cursor
	if (auto ws = cur_line.get_whitespace_span_at(i - 1); !ws.empty()) {
		i = ws.start_char_pos;
	}

	// 2. If not at start of line, determine backward word boundary or single terminator
	if (i > 0) {
		int target_pos = i - 1;
		if (cur_line.is_terminator_at(target_pos)) {
			i = target_pos;
		} else if (auto span = cur_line.get_word_span_at(target_pos); !span.empty()) {
			i = span.start_char_pos;
		} else {
			i = target_pos;
		}
	}

	cursor_x_ = i;
	update_target_cursor_x_unlocked();
	lock.unlock();
	notify_cursor_changed();
}
