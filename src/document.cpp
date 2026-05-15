#include "document.h"
#include <mutex>
#include <fstream>

document::document()
{
	for (int i = 0; i < 10; ++i) {
		lines_.emplace_back(""); // Start with 10 empty lines for testing
	}
}

document::document(const std::string& filename)
	: filename_(filename)
{
	if (!load_from_file(filename)) {
		for (int i = 0; i < 10; ++i) lines_.emplace_back("");
	}
}

bool document::load_from_file(const std::string& filename)
{
	std::unique_lock lock(mutex_);
	std::ifstream file(filename);
	if (!file.is_open()) {
		return false;
	}

	lines_.clear();
	std::string line_text;
	while (std::getline(file, line_text)) {
		lines_.emplace_back(line_text);
	}
	if (lines_.empty()) {
		lines_.emplace_back("");
	}
	
	filename_ = filename;
	modified_ = false;
	return true;
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

const line& document::get_line(size_t index) const
{
	std::shared_lock lock(mutex_);
	// In a real scenario, bounds checking should be handled carefully
	return lines_[index];
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

	int line_len = static_cast<int>(lines_[cursor_y_].get_text().length());
	if (cursor_x_ < 0) cursor_x_ = 0;
	if (cursor_x_ > line_len) cursor_x_ = line_len;
}
