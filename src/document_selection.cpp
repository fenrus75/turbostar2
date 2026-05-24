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
#include "git_manager.h"
#include "highlighter_registry.h"
#include "lsp_manager.h"

namespace fs = std::filesystem;

void document::set_selection_start()
{
	std::unique_lock lock(mutex_);
	selection_start_x_ = cursor_x_;
	selection_start_y_ = cursor_y_;
	lock.unlock();
	notify_cursor_changed();
}

void document::set_selection_end()
{
	std::unique_lock lock(mutex_);
	selection_end_x_ = cursor_x_;
	selection_end_y_ = cursor_y_;
	lock.unlock();
	notify_cursor_changed();
}

void document::set_selection(int start_y, int start_x, int end_y, int end_x)
{
	std::unique_lock lock(mutex_);
	selection_start_y_ = start_y;
	selection_start_x_ = start_x;
	selection_end_y_ = end_y;
	selection_end_x_ = end_x;
	lock.unlock();
	notify_cursor_changed();
}

void document::clear_selection()
{
	std::unique_lock lock(mutex_);
	selection_start_x_ = selection_start_y_ = -1;
	selection_end_x_ = selection_end_y_ = -1;
	lock.unlock();
	notify_cursor_changed();
}

std::vector<line> document::get_selection_block() const
{
	if (selection_start_y_ == -1 || selection_end_y_ == -1)
		return {};

	int sx, sy, ex, ey;
	if (selection_start_y_ < selection_end_y_ || (selection_start_y_ == selection_end_y_ && selection_start_x_ <= selection_end_x_)) {
		sx = selection_start_x_;
		sy = selection_start_y_;
		ex = selection_end_x_;
		ey = selection_end_y_;
	} else {
		sx = selection_end_x_;
		sy = selection_end_y_;
		ex = selection_start_x_;
		ey = selection_start_y_;
	}

	std::vector<line> block;
	if (sy == ey) {
		line l(lines_[sy]->get_text().substr(lines_[sy]->char_to_byte_offset(sx),
						     lines_[sy]->char_to_byte_offset(ex) - lines_[sy]->char_to_byte_offset(sx)));
		block.push_back(std::move(l));
	} else {
		line l1(lines_[sy]->get_text().substr(lines_[sy]->char_to_byte_offset(sx)));
		block.push_back(std::move(l1));
		for (int i = sy + 1; i < ey; ++i)
			block.push_back(*lines_[i]);
		line ln(lines_[ey]->get_text().substr(0, lines_[ey]->char_to_byte_offset(ex)));
		block.push_back(std::move(ln));
	}
	return block;
}

void document::delete_selection()
{
	std::unique_lock lock(mutex_);
	if (selection_start_y_ == -1 || selection_end_y_ == -1) {
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	int sx, sy, ex, ey;
	if (selection_start_y_ < selection_end_y_ || (selection_start_y_ == selection_end_y_ && selection_start_x_ <= selection_end_x_)) {
		sx = selection_start_x_;
		sy = selection_start_y_;
		ex = selection_end_x_;
		ey = selection_end_y_;
	} else {
		sx = selection_end_x_;
		sy = selection_end_y_;
		ex = selection_start_x_;
		ey = selection_start_y_;
	}

	begin_edit_group();

	if (sy == ey) {
		record_action(edit_action::action_type::replace_line, sy, lines_[sy]);
		for (int i = 0; i < (ex - sx); ++i)
			lines_[sy]->remove_at(sx);
		mark_line_dirty(lines_[sy]);
	} else {
		record_action(edit_action::action_type::replace_line, sy, lines_[sy]);
		// Record deletions in reverse order so undo (which reverses) inserts them correctly
		for (int i = ey; i > sy; --i) {
			record_action(edit_action::action_type::delete_line, i, lines_[i]);
		}

		line tail_line("");
		lines_[ey]->split_at(ex, tail_line);
		line throwaway("");
		lines_[sy]->split_at(sx, throwaway);
		lines_[sy]->merge(tail_line);
		mark_line_dirty(lines_[sy]);
		lines_.erase(lines_.begin() + sy + 1, lines_.begin() + ey + 1);
	}

	end_edit_group();

	cursor_x_ = sx;
	cursor_y_ = sy;
	selection_start_x_ = selection_start_y_ = -1;
	selection_end_x_ = selection_end_y_ = -1;
	set_modified();
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::copy_selection()
{
	std::unique_lock lock(mutex_);
	if (selection_start_y_ == -1 || selection_end_y_ == -1) {
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	std::vector<line> block = get_selection_block();
	int tx = cursor_x_;
	int ty = cursor_y_;

	begin_edit_group();
	insert_block(block);
	end_edit_group();

	selection_start_x_ = tx;
	selection_start_y_ = ty;
	selection_end_y_ = ty + block.size() - 1;
	if (block.size() == 1)
		selection_end_x_ = tx + block[0].length_in_chars();
	else
		selection_end_x_ = block.back().length_in_chars();

	set_modified();
	lock.unlock();
	notify_cursor_changed();
}

void document::move_selection()
{
	if (is_read_only())
		return;
	std::unique_lock lock(mutex_);
	if (selection_start_y_ == -1 || selection_end_y_ == -1) {
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	std::vector<line> block = get_selection_block();
	int tx = cursor_x_;
	int ty = cursor_y_;

	int sx, sy, ex, ey;
	get_selection_range_unlocked(sx, sy, ex, ey);

	if (ty > ey) {
		ty -= (ey - sy);
	} else if (ty == ey && tx >= ex) {
		ty = sy;
		tx = sx + (tx - ex);
	} else if (ty > sy || (ty == sy && tx >= sx)) {
		ty = sy;
		tx = sx;
	}

	begin_edit_group();

	lock.unlock();
	delete_selection();
	lock.lock();

	cursor_x_ = tx;
	cursor_y_ = ty;
	if (cursor_y_ >= line_count_unlocked())
		cursor_y_ = line_count_unlocked() - 1;
	int line_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ > line_len)
		cursor_x_ = line_len;

	int fx = cursor_x_;
	int fy = cursor_y_;
	insert_block(block);

	end_edit_group();

	selection_start_x_ = fx;
	selection_start_y_ = fy;
	selection_end_y_ = fy + block.size() - 1;
	if (block.size() == 1)
		selection_end_x_ = fx + block[0].length_in_chars();
	else
		selection_end_x_ = block.back().length_in_chars();

	set_modified();
	lock.unlock();
	notify_cursor_changed();
}

void document::insert_block(const std::vector<line> &block)
{
	if (block.empty())
		return;

	begin_edit_group();
	record_action(edit_action::action_type::replace_line, cursor_y_, lines_[cursor_y_]);

	line tail("");
	lines_[cursor_y_]->split_at(cursor_x_, tail);
	lines_[cursor_y_]->merge(block[0]);
	mark_line_dirty(lines_[cursor_y_]);
	if (block.size() > 1) {
		for (size_t i = 1; i < block.size(); ++i) {
			auto nl = std::make_shared<line>(block[i]);
			lines_.insert(lines_.begin() + cursor_y_ + i, nl);
			record_action(edit_action::action_type::insert_line, cursor_y_ + i, nullptr);
			mark_line_dirty(nl);
		}

		record_action(edit_action::action_type::replace_line, cursor_y_ + block.size() - 1, lines_[cursor_y_ + block.size() - 1]);
		lines_[cursor_y_ + block.size() - 1]->merge(tail);
		mark_line_dirty(lines_[cursor_y_ + block.size() - 1]);
	} else {
		lines_[cursor_y_]->merge(tail);
		mark_line_dirty(lines_[cursor_y_]);
	}
	cursor_y_ += block.size() - 1;
	if (block.size() == 1)
		cursor_x_ += block[0].length_in_chars();
	else
		cursor_x_ = block.back().length_in_chars();

	end_edit_group();
}

bool document::has_selection() const
{
	std::shared_lock lock(mutex_);
	return selection_start_y_ != -1 && selection_end_y_ != -1;
}

void document::get_selection_range_unlocked(int &start_x, int &start_y, int &end_x, int &end_y) const
{
	if (selection_start_y_ < selection_end_y_ || (selection_start_y_ == selection_end_y_ && selection_start_x_ <= selection_end_x_)) {
		start_x = selection_start_x_;
		start_y = selection_start_y_;
		end_x = selection_end_x_;
		end_y = selection_end_y_;
	} else {
		start_x = selection_end_x_;
		start_y = selection_end_y_;
		end_x = selection_start_x_;
		end_y = selection_start_y_;
	}
}

void document::get_selection_range(int &start_x, int &start_y, int &end_x, int &end_y) const
{
	std::shared_lock lock(mutex_);
	get_selection_range_unlocked(start_x, start_y, end_x, end_y);
}

void document::adjust_selection_for_insert(int y, int x, int count)
{
	auto adjust = [&](int &sx, int &sy) {
		if (sy == y) {
			if (sx >= x)
				sx += count;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

void document::adjust_selection_for_delete(int y, int x, int count)
{
	auto adjust = [&](int &sx, int &sy) {
		if (sy == y) {
			if (sx > x + count)
				sx -= count;
			else if (sx > x)
				sx = x;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

void document::adjust_selection_for_split(int y, int x)
{
	auto adjust = [&](int &sx, int &sy) {
		if (sy == y && sx >= x) {
			sy++;
			sx -= x;
		} else if (sy > y) {
			sy++;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

void document::adjust_selection_for_join(int y, int x)
{
	(void)x;
	int prev_line_char_len = lines_[y - 1]->length_in_chars();
	auto adjust = [&](int &sx, int &sy) {
		if (sy == y) {
			sy--;
			sx += prev_line_char_len;
		} else if (sy > y) {
			sy--;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

void document::adjust_selection_for_line_delete(int y)
{
	auto adjust = [&](int &sx, int &sy) {
		if (sy == y) {
			sx = 0;
			if (y >= line_count_unlocked() - 1) {
				sy = y - 1;
				sx = lines_[sy]->length_in_chars();
			}
		} else if (sy > y) {
			sy--;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}
