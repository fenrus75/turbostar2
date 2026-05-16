#include "document.h"
#include "event_logger.h"
#include <mutex>
#include <fstream>
#include <regex>

document::document(event_queue& global_queue)
	: global_queue_(global_queue)
{
	lines_.push_back(std::make_shared<line>(""));
	highlighter_thread_ = std::thread(&document::highlighter_thread_loop, this);
	log_state();
}

document::document(event_queue& global_queue, const std::string& filename)
	: filename_(filename), global_queue_(global_queue)
{
	if (filename.empty() || !load_from_file(filename)) {
		if (lines_.empty()) lines_.push_back(std::make_shared<line>(""));
	}
	highlighter_thread_ = std::thread(&document::highlighter_thread_loop, this);
	log_state();
}

document::~document()
{
	stop_thread_ = true;
	dirty_cv_.notify_all();
	if (highlighter_thread_.joinable()) {
		highlighter_thread_.join();
	}
}

bool document::load_from_file(const std::string& filename)
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
	modified_ = false;
	cursor_x_ = 0;
	cursor_y_ = 0;
	selection_start_x_ = selection_start_y_ = -1;
	selection_end_x_ = selection_end_y_ = -1;
	
	event_logger::get_instance().log("Document loaded from: " + filename + " (" + std::to_string(lines_.size()) + " lines)");
	lock.unlock();
	log_state();
	return true;
}


bool document::save()
{
	std::shared_lock lock(mutex_);
	std::string fname = filename_;
	lock.unlock();
	
	if (fname.empty()) {
		event_logger::get_instance().log("Save failed: No filename specified.");
		return false;
	}
	return save_to_file(fname);
}

bool document::save_to_file(const std::string& filename)
{
	std::unique_lock lock(mutex_);
	std::ofstream file(filename);
	if (!file.is_open()) {
		event_logger::get_instance().log("Save failed: Could not open file " + filename);
		return false;
	}

	for (size_t i = 0; i < lines_.size(); ++i) {
		file << lines_[i]->get_text();
		if (i < lines_.size() - 1) {
			file << "\n";
		}
	}
	
	filename_ = filename;
	modified_ = false;
	event_logger::get_instance().log("Document saved to: " + filename);
	lock.unlock();
	log_state();
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
	modified_ = false;
	cursor_x_ = 0;
	cursor_y_ = 0;
	selection_start_x_ = selection_start_y_ = -1;
	selection_end_x_ = selection_end_y_ = -1;
	lock.unlock();
	log_state();
}

const std::string& document::get_filename() const
{
	std::shared_lock lock(mutex_);
	return filename_;
}

bool document::is_modified() const
{
	std::shared_lock lock(mutex_);
	return modified_;
}

size_t document::get_line_count() const
{
	std::shared_lock lock(mutex_);
	return lines_.size();
}

std::shared_ptr<line> document::get_line(size_t index) const
{
	std::shared_lock lock(mutex_);
	if (index < lines_.size()) return lines_[index];
	return nullptr;
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

void document::move_cursor(int dx, int dy)
{
	std::unique_lock lock(mutex_);
	cursor_x_ += dx;
	cursor_y_ += dy;

	if (cursor_y_ < 0) cursor_y_ = 0;
	if (cursor_y_ >= static_cast<int>(lines_.size())) {
		cursor_y_ = static_cast<int>(lines_.size()) - 1;
	}

	int line_char_len = static_cast<int>(lines_[cursor_y_]->length_in_chars());
	if (cursor_x_ < 0) cursor_x_ = 0;
	if (cursor_x_ > line_char_len) cursor_x_ = line_char_len;
	
	event_logger::get_instance().log("Cursor moved to: " + std::to_string(cursor_y_) + ":" + std::to_string(cursor_x_));
	lock.unlock();
	log_state();
}

void document::insert_char(const std::string& utf8_char)
{
	std::unique_lock lock(mutex_);
	if (cursor_y_ >= 0 && cursor_y_ < static_cast<int>(lines_.size())) {
		adjust_selection_for_insert(cursor_y_, cursor_x_, 1);
		lines_[cursor_y_]->insert_at(cursor_x_, utf8_char);
		mark_line_dirty(lines_[cursor_y_]);
		cursor_x_++;
		set_modified();
	}
	lock.unlock();
	log_state();
}

void document::backspace()
{
	std::unique_lock lock(mutex_);
	if (cursor_x_ > 0) {
		adjust_selection_for_delete(cursor_y_, cursor_x_ - 1, 1);
		lines_[cursor_y_]->remove_at(cursor_x_ - 1);
		mark_line_dirty(lines_[cursor_y_]);
		cursor_x_--;
		set_modified();
	} else if (cursor_y_ > 0) {
		// Join with previous line
		int prev_line_idx = cursor_y_ - 1;
		int prev_line_char_len = static_cast<int>(lines_[prev_line_idx]->length_in_chars());
		
		adjust_selection_for_join(cursor_y_, cursor_x_);
		
		lines_[prev_line_idx]->merge(*lines_[cursor_y_]);
		mark_line_dirty(lines_[prev_line_idx]);
		lines_.erase(lines_.begin() + cursor_y_);
		
		cursor_y_ = prev_line_idx;
		cursor_x_ = prev_line_char_len;
		set_modified();
	}
	lock.unlock();
	log_state();
}

void document::delete_char()
{
	std::unique_lock lock(mutex_);
	int line_char_len = static_cast<int>(lines_[cursor_y_]->length_in_chars());
	if (cursor_x_ < line_char_len) {
		adjust_selection_for_delete(cursor_y_, cursor_x_, 1);
		lines_[cursor_y_]->remove_at(cursor_x_);
		mark_line_dirty(lines_[cursor_y_]);
		set_modified();
	} else if (cursor_y_ < static_cast<int>(lines_.size()) - 1) {
		// Join next line into this one
		int next_line_idx = cursor_y_ + 1;
		adjust_selection_for_join(next_line_idx, 0);
		lines_[cursor_y_]->merge(*lines_[next_line_idx]);
		mark_line_dirty(lines_[cursor_y_]);
		lines_.erase(lines_.begin() + next_line_idx);
		set_modified();
	}
	lock.unlock();
	log_state();
}

void document::delete_to_eol()
{
	std::unique_lock lock(mutex_);
	int line_char_len = static_cast<int>(lines_[cursor_y_]->length_in_chars());
	int count = line_char_len - cursor_x_;
	if (count > 0) {
		adjust_selection_for_delete(cursor_y_, cursor_x_, count);
		for (int i = 0; i < count; ++i) {
			lines_[cursor_y_]->remove_at(cursor_x_);
		}
		mark_line_dirty(lines_[cursor_y_]);
		set_modified();
	}
	lock.unlock();
	log_state();
}

void document::delete_to_bol()
{
	std::unique_lock lock(mutex_);
	int count = cursor_x_;
	if (count > 0) {
		adjust_selection_for_delete(cursor_y_, 0, count);
		for (int i = 0; i < count; ++i) {
			lines_[cursor_y_]->remove_at(0);
		}
		mark_line_dirty(lines_[cursor_y_]);
		cursor_x_ = 0;
		set_modified();
	}
	lock.unlock();
	log_state();
}

void document::delete_word_forward()
{
	std::unique_lock lock(mutex_);
	int line_char_len = static_cast<int>(lines_[cursor_y_]->length_in_chars());
	if (cursor_x_ >= line_char_len) {
		lock.unlock();
		log_state();
		return;
	}

	std::string text = lines_[cursor_y_]->get_text();
	auto get_char_at = [&](int idx) -> char {
		size_t offset = lines_[cursor_y_]->char_to_byte_offset(idx);
		if (offset < text.length()) return text[offset];
		return 0;
	};

	int i = cursor_x_;
	while (i < line_char_len && !std::isspace(static_cast<unsigned char>(get_char_at(i)))) i++;
	while (i < line_char_len && std::isspace(static_cast<unsigned char>(get_char_at(i)))) i++;
	
	int count = i - cursor_x_;
	if (count > 0) {
		adjust_selection_for_delete(cursor_y_, cursor_x_, count);
		for (int j = 0; j < count; ++j) {
			lines_[cursor_y_]->remove_at(cursor_x_);
		}
		mark_line_dirty(lines_[cursor_y_]);
		set_modified();
	}
	lock.unlock();
	log_state();
}

void document::delete_word_backward()
{
	std::unique_lock lock(mutex_);
	if (cursor_x_ == 0) {
		lock.unlock();
		log_state();
		return;
	}

	std::string text = lines_[cursor_y_]->get_text();
	auto get_char_at = [&](int idx) -> char {
		size_t offset = lines_[cursor_y_]->char_to_byte_offset(idx);
		if (offset < text.length()) return text[offset];
		return 0;
	};

	int i = cursor_x_ - 1;
	while (i > 0 && std::isspace(static_cast<unsigned char>(get_char_at(i)))) i--;
	while (i > 0 && !std::isspace(static_cast<unsigned char>(get_char_at(i - 1)))) i--;
	
	int count = cursor_x_ - i;
	if (count > 0) {
		adjust_selection_for_delete(cursor_y_, i, count);
		for (int j = 0; j < count; ++j) {
			lines_[cursor_y_]->remove_at(i);
		}
		mark_line_dirty(lines_[cursor_y_]);
		cursor_x_ = i;
		set_modified();
	}
	lock.unlock();
	log_state();
}

void document::split_line()
{
	std::unique_lock lock(mutex_);
	if (cursor_y_ >= 0 && cursor_y_ < static_cast<int>(lines_.size())) {
		adjust_selection_for_split(cursor_y_, cursor_x_);
		auto new_l = std::make_shared<line>("");
		lines_[cursor_y_]->split_at(cursor_x_, *new_l);
		mark_line_dirty(lines_[cursor_y_]);
		mark_line_dirty(new_l);
		lines_.insert(lines_.begin() + cursor_y_ + 1, new_l);
		cursor_y_++;
		cursor_x_ = 0;
		set_modified();
	}
	lock.unlock();
	log_state();
}

void document::move_to_bol()
{
	std::unique_lock lock(mutex_);
	cursor_x_ = 0;
	lock.unlock();
	log_state();
}

void document::move_to_eol()
{
	std::unique_lock lock(mutex_);
	if (cursor_y_ >= 0 && cursor_y_ < static_cast<int>(lines_.size())) {
		cursor_x_ = static_cast<int>(lines_[cursor_y_]->length_in_chars());
	}
	lock.unlock();
	log_state();
}

void document::move_to_top()
{
	std::unique_lock lock(mutex_);
	cursor_x_ = 0;
	cursor_y_ = 0;
	lock.unlock();
	log_state();
}

void document::move_to_bottom()
{
	std::unique_lock lock(mutex_);
	cursor_y_ = static_cast<int>(lines_.size()) - 1;
	cursor_x_ = static_cast<int>(lines_[cursor_y_]->length_in_chars());
	lock.unlock();
	log_state();
}

void document::move_page_up(int page_height)
{
	std::unique_lock lock(mutex_);
	cursor_y_ -= page_height;
	if (cursor_y_ < 0) cursor_y_ = 0;
	
	int line_char_len = static_cast<int>(lines_[cursor_y_]->length_in_chars());
	if (cursor_x_ > line_char_len) cursor_x_ = line_char_len;
	lock.unlock();
	log_state();
}

void document::move_page_down(int page_height)
{
	std::unique_lock lock(mutex_);
	cursor_y_ += page_height;
	if (cursor_y_ >= static_cast<int>(lines_.size())) {
		cursor_y_ = static_cast<int>(lines_.size()) - 1;
	}
	
	int line_char_len = static_cast<int>(lines_[cursor_y_]->length_in_chars());
	if (cursor_x_ > line_char_len) cursor_x_ = line_char_len;
	lock.unlock();
	log_state();
}

void document::move_next_word()
{
	std::unique_lock lock(mutex_);
	std::string text = lines_[cursor_y_]->get_text();
	int line_char_len = static_cast<int>(lines_[cursor_y_]->length_in_chars());
	
	if (cursor_x_ >= line_char_len) {
		if (cursor_y_ < static_cast<int>(lines_.size()) - 1) {
			cursor_y_++;
			cursor_x_ = 0;
		}
		lock.unlock();
		log_state();
		return;
	}

	auto get_char_at = [&](int idx) -> char {
		size_t offset = lines_[cursor_y_]->char_to_byte_offset(idx);
		if (offset < text.length()) return text[offset];
		return 0;
	};

	int i = cursor_x_;
	while (i < line_char_len && !std::isspace(static_cast<unsigned char>(get_char_at(i)))) i++;
	while (i < line_char_len && std::isspace(static_cast<unsigned char>(get_char_at(i)))) i++;
	
	if (i >= line_char_len && cursor_y_ < static_cast<int>(lines_.size()) - 1) {
		cursor_y_++;
		cursor_x_ = 0;
	} else {
		cursor_x_ = i;
	}
	
	lock.unlock();
	log_state();
}

void document::move_prev_word()
{
	std::unique_lock lock(mutex_);
	if (cursor_x_ == 0) {
		if (cursor_y_ > 0) {
			cursor_y_--;
			cursor_x_ = static_cast<int>(lines_[cursor_y_]->length_in_chars());
		}
		lock.unlock();
		log_state();
		return;
	}

	std::string text = lines_[cursor_y_]->get_text();
	auto get_char_at = [&](int idx) -> char {
		size_t offset = lines_[cursor_y_]->char_to_byte_offset(idx);
		if (offset < text.length()) return text[offset];
		return 0;
	};

	int i = cursor_x_ - 1;
	while (i > 0 && std::isspace(static_cast<unsigned char>(get_char_at(i)))) i--;
	while (i > 0 && !std::isspace(static_cast<unsigned char>(get_char_at(i - 1)))) i--;
	
	cursor_x_ = i;
	lock.unlock();
	log_state();
}

void document::delete_line()
{
	std::unique_lock lock(mutex_);
	if (lines_.size() <= 1) {
		lines_[0]->set_text("");
		mark_line_dirty(lines_[0]);
		selection_start_x_ = selection_start_y_ = -1;
		selection_end_x_ = selection_end_y_ = -1;
		cursor_x_ = 0;
		cursor_y_ = 0;
		set_modified();
		lock.unlock();
		log_state();
		return;
	}

	adjust_selection_for_line_delete(cursor_y_);
	lines_.erase(lines_.begin() + cursor_y_);
	if (cursor_y_ >= static_cast<int>(lines_.size())) {
		cursor_y_ = static_cast<int>(lines_.size()) - 1;
	}
	
	cursor_x_ = 0;
	set_modified();
	lock.unlock();
	log_state();
}

void document::set_selection_start()
{
	std::unique_lock lock(mutex_);
	selection_start_x_ = cursor_x_;
	selection_start_y_ = cursor_y_;
	lock.unlock();
	log_state();
}

void document::set_selection_end()
{
	std::unique_lock lock(mutex_);
	selection_end_x_ = cursor_x_;
	selection_end_y_ = cursor_y_;
	lock.unlock();
	log_state();
}

void document::clear_selection()
{
	std::unique_lock lock(mutex_);
	selection_start_x_ = selection_start_y_ = -1;
	selection_end_x_ = selection_end_y_ = -1;
	lock.unlock();
	log_state();
}

std::vector<line> document::get_selection_block() const
{
	if (selection_start_y_ == -1 || selection_end_y_ == -1) return {};

	int sx, sy, ex, ey;
	if (selection_start_y_ < selection_end_y_ || (selection_start_y_ == selection_end_y_ && selection_start_x_ <= selection_end_x_)) {
		sx = selection_start_x_; sy = selection_start_y_;
		ex = selection_end_x_; ey = selection_end_y_;
	} else {
		sx = selection_end_x_; sy = selection_end_y_;
		ex = selection_start_x_; ey = selection_start_y_;
	}

	std::vector<line> block;
	if (sy == ey) {
		line l(lines_[sy]->get_text().substr(lines_[sy]->char_to_byte_offset(sx), 
		                                     lines_[sy]->char_to_byte_offset(ex) - lines_[sy]->char_to_byte_offset(sx)));
		block.push_back(std::move(l));
	} else {
		line l1(lines_[sy]->get_text().substr(lines_[sy]->char_to_byte_offset(sx)));
		block.push_back(std::move(l1));
		for (int i = sy + 1; i < ey; ++i) block.push_back(*lines_[i]);
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
		log_state();
		return;
	}

	int sx, sy, ex, ey;
	if (selection_start_y_ < selection_end_y_ || (selection_start_y_ == selection_end_y_ && selection_start_x_ <= selection_end_x_)) {
		sx = selection_start_x_; sy = selection_start_y_;
		ex = selection_end_x_; ey = selection_end_y_;
	} else {
		sx = selection_end_x_; sy = selection_end_y_;
		ex = selection_start_x_; ey = selection_start_y_;
	}

	if (sy == ey) {
		for (int i = 0; i < (ex - sx); ++i) lines_[sy]->remove_at(sx);
		mark_line_dirty(lines_[sy]);
	} else {
		line tail_line("");
		lines_[ey]->split_at(ex, tail_line);
		line throwaway("");
		lines_[sy]->split_at(sx, throwaway);
		lines_[sy]->merge(tail_line);
		mark_line_dirty(lines_[sy]);
		lines_.erase(lines_.begin() + sy + 1, lines_.begin() + ey + 1);
	}

	cursor_x_ = sx;
	cursor_y_ = sy;
	selection_start_x_ = selection_start_y_ = -1;
	selection_end_x_ = selection_end_y_ = -1;
	set_modified();
	lock.unlock();
	log_state();
}

void document::copy_selection()
{
	std::unique_lock lock(mutex_);
	if (selection_start_y_ == -1 || selection_end_y_ == -1) {
		lock.unlock();
		log_state();
		return;
	}

	std::vector<line> block = get_selection_block();
	int tx = cursor_x_;
	int ty = cursor_y_;
	insert_block(block);
	
	selection_start_x_ = tx;
	selection_start_y_ = ty;
	selection_end_y_ = ty + block.size() - 1;
	if (block.size() == 1) selection_end_x_ = tx + block[0].length_in_chars();
	else selection_end_x_ = block.back().length_in_chars();

	set_modified();
	lock.unlock();
	log_state();
}

void document::move_selection()
{
	std::unique_lock lock(mutex_);
	if (selection_start_y_ == -1 || selection_end_y_ == -1) {
		lock.unlock();
		log_state();
		return;
	}

	std::vector<line> block = get_selection_block();
	int tx = cursor_x_;
	int ty = cursor_y_;
	
	lock.unlock();
	delete_selection();
	lock.lock();
	
	cursor_x_ = tx; cursor_y_ = ty;
	if (cursor_y_ >= static_cast<int>(lines_.size())) cursor_y_ = lines_.size() - 1;
	int line_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ > line_len) cursor_x_ = line_len;

	int fx = cursor_x_; int fy = cursor_y_;
	insert_block(block);

	selection_start_x_ = fx; selection_start_y_ = fy;
	selection_end_y_ = fy + block.size() - 1;
	if (block.size() == 1) selection_end_x_ = fx + block[0].length_in_chars();
	else selection_end_x_ = block.back().length_in_chars();

	set_modified();
	lock.unlock();
	log_state();
}

void document::insert_block(const std::vector<line>& block)
{
	if (block.empty()) return;
	line tail("");
	lines_[cursor_y_]->split_at(cursor_x_, tail);
	lines_[cursor_y_]->merge(block[0]);
	mark_line_dirty(lines_[cursor_y_]);
	if (block.size() > 1) {
		for (size_t i = 1; i < block.size(); ++i) {
			auto nl = std::make_shared<line>(block[i]);
			lines_.insert(lines_.begin() + cursor_y_ + i, nl);
			mark_line_dirty(nl);
		}
		lines_[cursor_y_ + block.size() - 1]->merge(tail);
		mark_line_dirty(lines_[cursor_y_ + block.size() - 1]);
	} else {
		lines_[cursor_y_]->merge(tail);
		mark_line_dirty(lines_[cursor_y_]);
	}
	cursor_y_ += block.size() - 1;
	if (block.size() == 1) cursor_x_ += block[0].length_in_chars();
	else cursor_x_ = block.back().length_in_chars();
}

bool document::has_selection() const
{
	std::shared_lock lock(mutex_);
	return selection_start_y_ != -1 && selection_end_y_ != -1;
}

void document::get_selection_range(int& start_x, int& start_y, int& end_x, int& end_y) const
{
	std::shared_lock lock(mutex_);
	if (selection_start_y_ < selection_end_y_ || (selection_start_y_ == selection_end_y_ && selection_start_x_ <= selection_end_x_)) {
		start_x = selection_start_x_; start_y = selection_start_y_;
		end_x = selection_end_x_; end_y = selection_end_y_;
	} else {
		start_x = selection_end_x_; start_y = selection_end_y_;
		end_x = selection_start_x_; end_y = selection_start_y_;
	}
}

void document::log_state() const
{
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

void document::set_modified()
{
	modified_ = true;
}

void document::adjust_selection_for_insert(int y, int x, int count)
{
	auto adjust = [&](int& sx, int& sy) {
		if (sy == y) {
			if (sx >= x) sx += count;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

void document::adjust_selection_for_delete(int y, int x, int count)
{
	auto adjust = [&](int& sx, int& sy) {
		if (sy == y) {
			if (sx > x + count) sx -= count;
			else if (sx > x) sx = x;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

void document::adjust_selection_for_split(int y, int x)
{
	auto adjust = [&](int& sx, int& sy) {
		if (sy == y && sx >= x) { sy++; sx -= x; }
		else if (sy > y) { sy++; }
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

void document::adjust_selection_for_join(int y, int x)
{
	(void)x;
	int prev_line_char_len = static_cast<int>(lines_[y - 1]->length_in_chars());
	auto adjust = [&](int& sx, int& sy) {
		if (sy == y) { sy--; sx += prev_line_char_len; }
		else if (sy > y) { sy--; }
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

void document::adjust_selection_for_line_delete(int y)
{
	auto adjust = [&](int& sx, int& sy) {
		if (sy == y) {
			sx = 0; 
			if (y >= static_cast<int>(lines_.size()) - 1) {
				sy = y - 1;
				sx = static_cast<int>(lines_[sy]->length_in_chars());
			}
		} else if (sy > y) {
			sy--;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

bool document::find_next(const search_params& params, bool is_repeat)
{
	if (params.query.empty()) return false;
	
	std::unique_lock lock(mutex_);
	int start_y = cursor_y_;
	int start_x = cursor_x_;

	int scope_sy = 0, scope_sx = 0, scope_ey = static_cast<int>(lines_.size()) - 1, scope_ex = static_cast<int>(lines_.back()->length_in_chars());
	if (params.selected_text_only && selection_start_y_ != -1) {
		get_selection_range(scope_sx, scope_sy, scope_ex, scope_ey);
	}

	if (!params.from_cursor) {
		if (params.backward) {
			start_y = scope_ey;
			start_x = scope_ex;
		} else {
			start_y = scope_sy;
			start_x = scope_sx;
		}
	} else if (is_repeat) {
		// Step over current char
		if (params.backward) {
			if (start_x > 0) start_x--;
			else if (start_y > scope_sy) {
				start_y--;
				start_x = static_cast<int>(lines_[start_y]->length_in_chars());
			} else return false;
		} else {
			if (start_x < static_cast<int>(lines_[start_y]->length_in_chars())) start_x++;
			else if (start_y < scope_ey) {
				start_y++;
				start_x = 0;
			} else return false;
		}
	}
	
	auto check_line = [&](int y, int x_limit) -> int {
		std::string line_text = lines_[y]->get_text();
		std::string original_line_text = line_text;
		
		std::regex_constants::syntax_option_type flags = std::regex::ECMAScript;
		if (params.ignore_case) flags |= std::regex::icase;

		std::string pattern = params.query;
		if (params.whole_words && !params.regex) {
			pattern = "\\b" + pattern + "\\b";
		}

		try {
			std::regex re(pattern, flags);
			auto words_begin = std::sregex_iterator(line_text.begin(), line_text.end(), re);
			auto words_end = std::sregex_iterator();

			int best_found_char_idx = -1;
			size_t byte_limit = lines_[y]->char_to_byte_offset(x_limit);
			
			size_t line_scope_start_byte = 0;
			size_t line_scope_end_byte = line_text.length();
			if (params.selected_text_only) {
				if (y == scope_sy) line_scope_start_byte = lines_[y]->char_to_byte_offset(scope_sx);
				if (y == scope_ey) line_scope_end_byte = lines_[y]->char_to_byte_offset(scope_ex);
			}

			for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
				std::smatch match = *i;
				size_t byte_pos = match.position();

				if (params.backward) {
					if (byte_pos >= line_scope_start_byte && byte_pos <= byte_limit) {
						int found_char_idx = 0;
						size_t current_byte = 0;
						while (current_byte < byte_pos && current_byte < original_line_text.length()) {
							unsigned char c = static_cast<unsigned char>(original_line_text[current_byte]);
							if (c < 0x80) current_byte += 1;
							else if ((c & 0xE0) == 0xC0) current_byte += 2;
							else if ((c & 0xE0) == 0xE0) current_byte += 3;
							else if ((c & 0xF0) == 0xF0) current_byte += 4;
							else current_byte += 1;
							found_char_idx++;
						}
						best_found_char_idx = found_char_idx;
					}
				} else {
					if (byte_pos >= byte_limit && byte_pos < line_scope_end_byte) {
						int found_char_idx = 0;
						size_t current_byte = 0;
						while (current_byte < byte_pos && current_byte < original_line_text.length()) {
							unsigned char c = static_cast<unsigned char>(original_line_text[current_byte]);
							if (c < 0x80) current_byte += 1;
							else if ((c & 0xE0) == 0xC0) current_byte += 2;
							else if ((c & 0xE0) == 0xE0) current_byte += 3;
							else if ((c & 0xF0) == 0xF0) current_byte += 4;
							else current_byte += 1;
							found_char_idx++;
						}
						return found_char_idx;
					}
				}
			}
			return best_found_char_idx;
		} catch (...) {
			return -1;
		}
	};

	if (params.backward) {
		for (int y = start_y; y >= scope_sy; --y) {
			int x_lim;
			if (y == start_y) {
				x_lim = start_x;
			} else {
				x_lim = static_cast<int>(lines_[y]->length_in_chars());
			}
			int found_x = check_line(y, x_lim);
			if (found_x != -1) {
				cursor_y_ = y;
				cursor_x_ = found_x;
				lock.unlock();
				log_state();
				return true;
			}
		}
	} else {
		for (int y = start_y; y <= scope_ey; ++y) {
			int x_lim;
			if (y == start_y) {
				x_lim = start_x;
			} else {
				x_lim = 0;
			}
			int found_x = check_line(y, x_lim);
			if (found_x != -1) {
				cursor_y_ = y;
				cursor_x_ = found_x;
				lock.unlock();
				log_state();
				return true;
			}
		}
	}
	
	lock.unlock();
	log_state();
	return false;
}

void document::mark_line_dirty(std::shared_ptr<line> l)
{
	std::lock_guard lock(dirty_mutex_);
	dirty_lines_.push(l);
	dirty_cv_.notify_one();
}

void document::highlighter_thread_loop()
{
	while (!stop_thread_) {
		std::shared_ptr<line> l;
		{
			std::unique_lock lock(dirty_mutex_);
			dirty_cv_.wait(lock, [&]{ return !dirty_lines_.empty() || stop_thread_; });
			if (stop_thread_) break;
			l = dirty_lines_.front();
			dirty_lines_.pop();
		}
		if (l) {
			process_line_highlight(l);
			
			// If no more lines in queue, request a redraw
			{
				std::lock_guard lock(dirty_mutex_);
				if (dirty_lines_.empty()) {
					editor_event ev;
					ev.type = event_type::redraw;
					global_queue_.push(ev);
				}
			}
		}
	}
}

void document::process_line_highlight(std::shared_ptr<line> l)
{
	std::string text = l->get_text();
	size_t char_count = l->length_in_chars();
	std::vector<syntax_attribute> attrs(char_count, syntax_attribute::normal);

	// Pre-compiled combined regex for efficiency
	static const std::regex kw_regex("\\b(void|int|char|const|bool|class|struct|enum|virtual|override|return|if|else|for|while)\\b");

	auto words_begin = std::sregex_iterator(text.begin(), text.end(), kw_regex);
	auto words_end = std::sregex_iterator();

	for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
		std::smatch match = *i;
		size_t byte_pos = match.position();
		size_t byte_len = match.length();

		// Convert byte pos to char pos
		int char_pos = 0;
		size_t current_byte = 0;
		while (current_byte < byte_pos && char_pos < static_cast<int>(char_count)) {
			unsigned char c = static_cast<unsigned char>(text[current_byte]);
			if (c < 0x80) current_byte += 1;
			else if ((c & 0xE0) == 0xC0) current_byte += 2;
			else if ((c & 0xE0) == 0xE0) current_byte += 3;
			else if ((c & 0xF0) == 0xF0) current_byte += 4;
			else current_byte += 1;
			char_pos++;
		}

		// Mark as keyword
		for (size_t j = 0; j < byte_len; ++j) {
			if (char_pos + j < attrs.size()) {
				attrs[char_pos + j] = syntax_attribute::keyword;
			}
		}
	}

	l->set_attributes(attrs);
}
