#include "document.h"
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

namespace fs = std::filesystem;


void document::move_cursor(int dx, int dy)
{
	std::unique_lock lock(mutex_);

	if (dx != 0) {
		if (dx < 0 && cursor_x_ == 0 && cursor_y_ > 0) {
			cursor_y_--;
			cursor_x_ = lines_[cursor_y_]->length_in_chars();
		} else if (dx > 0 && cursor_x_ >= lines_[cursor_y_]->length_in_chars() &&
			   cursor_y_ < line_count_unlocked() - 1) {
			cursor_y_++;
			cursor_x_ = 0;
		} else {
			cursor_x_ += dx;
		}
		target_cursor_x_ = cursor_x_;
	}

	if (dy != 0) {
		cursor_y_ += dy;
		cursor_x_ = target_cursor_x_; // Attempt to use target X when moving vertically
	}

	if (cursor_y_ < 0)
		cursor_y_ = 0;
	if (cursor_y_ >= line_count_unlocked())
		cursor_y_ = line_count_unlocked() - 1;

	int line_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ < 0)
		cursor_x_ = 0;
	if (cursor_x_ > line_len)
		cursor_x_ = line_len;

	lock.unlock();
	notify_cursor_changed();
}


void document::move_to_bol()
{
	std::unique_lock lock(mutex_);
	cursor_x_ = 0;
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}


void document::move_to_eol()
{
	std::unique_lock lock(mutex_);
	if (cursor_y_ >= 0 && cursor_y_ < line_count_unlocked()) {
		cursor_x_ = lines_[cursor_y_]->length_in_chars();
	}
	lock.unlock();
	notify_cursor_changed();
}


void document::move_to_top()
{
	std::unique_lock lock(mutex_);
	cursor_x_ = 0;
	cursor_y_ = 0;
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}


void document::move_to_bottom()
{
	std::unique_lock lock(mutex_);
	cursor_y_ = line_count_unlocked() - 1;
	cursor_x_ = lines_[cursor_y_]->length_in_chars();
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}


void document::move_page_up(int page_height)
{
	std::unique_lock lock(mutex_);
	cursor_y_ -= page_height;
	cursor_y_ = std::max(0, cursor_y_);
	cursor_x_ = target_cursor_x_;

	int line_char_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ > line_char_len)
		cursor_x_ = line_char_len;
	lock.unlock();
	notify_cursor_changed();
}


void document::move_page_down(int page_height)
{
	std::unique_lock lock(mutex_);
	cursor_y_ += page_height;
	if (cursor_y_ >= line_count_unlocked()) {
		cursor_y_ = line_count_unlocked() - 1;
	}
	cursor_x_ = target_cursor_x_;

	int line_char_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ > line_char_len)
		cursor_x_ = line_char_len;
	lock.unlock();
	notify_cursor_changed();
}


void document::move_next_word()
{
	std::unique_lock lock(mutex_);
	std::string text = lines_[cursor_y_]->get_text();
	int line_char_len = lines_[cursor_y_]->length_in_chars();

	if (cursor_x_ >= line_char_len) {
		if (cursor_y_ < line_count_unlocked() - 1) {
			cursor_y_++;
			cursor_x_ = 0;
		}
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	int i = cursor_x_;
	while (i < line_char_len && !is_space_at_unlocked(cursor_y_, i))
		i++;
	while (i < line_char_len && is_space_at_unlocked(cursor_y_, i))
		i++;

	if (i >= line_char_len && cursor_y_ < line_count_unlocked() - 1) {
		cursor_y_++;
		cursor_x_ = 0;
	} else {
		cursor_x_ = i;
	}

	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}


void document::move_prev_word()
{
	std::unique_lock lock(mutex_);
	if (cursor_x_ == 0) {
		if (cursor_y_ > 0) {
			cursor_y_--;
			cursor_x_ = lines_[cursor_y_]->length_in_chars();
		}
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	int i = cursor_x_ - 1;
	while (i > 0 && is_space_at_unlocked(cursor_y_, i))
		i--;
	while (i > 0 && !is_space_at_unlocked(cursor_y_, i - 1))
		i--;

	cursor_x_ = i;
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}
