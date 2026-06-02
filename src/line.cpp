#include "line.h"
#include <algorithm>
#include <string>
#include <mutex>
#include "utf8.h"

line::line(const std::string &text) : text_(text)
{
	// Initialize attributes to normal for all chars
	attributes_.assign(length_in_chars(), syntax_attribute::normal);
}

line::line(const line &other)
{
	std::shared_lock lock(other.mutex_);
	text_ = other.text_;
	attributes_ = other.attributes_;
}

line &line::operator=(const line &other)
{
	if (this != &other) {
		std::unique_lock lock_this(mutex_, std::defer_lock);
		std::shared_lock lock_other(other.mutex_, std::defer_lock);
		std::lock(lock_this, lock_other);
		text_ = other.text_;
		attributes_ = other.attributes_;
	}
	return *this;
}

line::line(line &&other) noexcept
{
	std::unique_lock lock(other.mutex_);
	text_ = std::move(other.text_);
	attributes_ = std::move(other.attributes_);
}

line &line::operator=(line &&other) noexcept
{
	if (this != &other) {
		std::unique_lock lock_this(mutex_, std::defer_lock);
		std::unique_lock lock_other(other.mutex_, std::defer_lock);
		std::lock(lock_this, lock_other);
		text_ = std::move(other.text_);
		attributes_ = std::move(other.attributes_);
	}
	return *this;
}

std::string line::get_text() const
{
	std::shared_lock lock(mutex_);
	return text_;
}

void line::get_content(std::string &out_text, std::vector<syntax_attribute> &out_attrs) const
{
	std::shared_lock lock(mutex_);
	out_text = text_;
	out_attrs = attributes_;
}

void line::set_text(const std::string &text)
{
	std::unique_lock lock(mutex_);
	text_ = text;
	attributes_.assign(utf8::length(text_), syntax_attribute::normal);
}

size_t line::char_to_byte_offset(int char_pos) const
{
	std::shared_lock lock(mutex_);
	return utf8::char_to_byte_offset(text_, char_pos);
}

int line::length_in_chars() const
{
	std::shared_lock lock(mutex_);
	return utf8::length(text_);
}

bool line::next_utf8_character(size_t &byte_offset, std::string &out_char) const
{
	std::shared_lock lock(mutex_);
	return utf8::next_character(text_, byte_offset, out_char);
}

void line::insert_at(int char_pos, const std::string &utf8_char)
{
	if (char_pos < 0)
		char_pos = 0;
	std::unique_lock lock(mutex_);
	if (char_pos > static_cast<int>(attributes_.size())) {
		char_pos = static_cast<int>(attributes_.size());
	}

	size_t offset = utf8::char_to_byte_offset(text_, char_pos);

	if (offset <= text_.length()) {
		text_.insert(offset, utf8_char);
		if (char_pos <= static_cast<int>(attributes_.size())) {
			attributes_.insert(attributes_.begin() + char_pos, syntax_attribute::normal);
		} else {
			attributes_.push_back(syntax_attribute::normal);
		}
	}
}

void line::remove_at(int char_pos)
{
	if (char_pos < 0)
		return;
	std::unique_lock lock(mutex_);
	if (char_pos >= static_cast<int>(attributes_.size()))
		return;

	size_t offset = utf8::char_to_byte_offset(text_, char_pos);

	if (offset < text_.length()) {
		size_t next_offset = offset + utf8::char_len(byte_at_unlocked(offset));
		text_.erase(offset, next_offset - offset);
		if (char_pos < static_cast<int>(attributes_.size())) {
			attributes_.erase(attributes_.begin() + char_pos);
		}
	}
}

void line::split_at(int char_pos, line &new_line)
{
	if (char_pos < 0)
		char_pos = 0;
	std::unique_lock lock_this(mutex_, std::defer_lock);
	std::unique_lock lock_other(new_line.mutex_, std::defer_lock);
	std::lock(lock_this, lock_other);
	if (char_pos > static_cast<int>(attributes_.size())) {
		char_pos = static_cast<int>(attributes_.size());
	}

	size_t offset = utf8::char_to_byte_offset(text_, char_pos);

	if (offset <= text_.length()) {
		new_line.text_ = text_.substr(offset);
		text_.erase(offset);

		if (char_pos < static_cast<int>(attributes_.size())) {
			std::vector<syntax_attribute> new_attrs(attributes_.begin() + char_pos, attributes_.end());
			new_line.attributes_ = new_attrs;
			attributes_.erase(attributes_.begin() + char_pos, attributes_.end());
		}
	}
}

void line::merge(const line &other_line)
{
	std::unique_lock lock_this(mutex_, std::defer_lock);
	std::shared_lock lock_other(other_line.mutex_, std::defer_lock);
	std::lock(lock_this, lock_other);

	text_ += other_line.text_;
	attributes_.assign(utf8::length(text_), syntax_attribute::normal);
}

int line::char_to_display_col(int char_pos) const
{
	std::shared_lock lock(mutex_);
	int col = 0;
	int current_char = 0;
	size_t byte_offset = 0;

	while (current_char < char_pos && byte_offset < text_.length()) {
		unsigned char c = byte_at(static_cast<int>(byte_offset));
		size_t char_bytes = utf8::char_len(c);

		if (c < 0x80) {
			if (c == '\t') {
				col = (col / 8 + 1) * 8;
			} else {
				col += 1;
			}
		} else {
			col += 1; // Multi-byte characters display as 1 cell wide for now
		}

		byte_offset += char_bytes;
		current_char++;
	}
	return col;
}

int line::display_col_to_char_pos(int display_col) const
{
	std::shared_lock lock(mutex_);
	int len = length_in_chars();
	int best_char_pos = 0;
	int best_diff = 999999;
	for (int pos = 0; pos <= len; ++pos) {
		int col = char_to_display_col(pos);
		int diff = std::abs(col - display_col);
		if (diff < best_diff) {
			best_diff = diff;
			best_char_pos = pos;
		}
		if (col >= display_col) {
			break;
		}
	}
	return best_char_pos;
}

unsigned char line::byte_at(int offset) const
{
	std::shared_lock lock(mutex_);
	return byte_at_unlocked(offset);
}

unsigned char line::byte_at_unlocked(int offset) const
{
	if (offset >= 0 && offset < static_cast<int>(text_.length())) {
		return static_cast<unsigned char>(text_[offset]);
	}
	return 0;
}

void line::set_attributes(const std::vector<syntax_attribute> &attrs)
{
	std::unique_lock lock(mutex_);
	attributes_ = attrs;
}

syntax_attribute line::get_attribute(int char_pos) const
{
	std::shared_lock lock(mutex_);
	if (char_pos >= 0 && char_pos < static_cast<int>(attributes_.size())) {
		return attributes_[char_pos];
	}
	return syntax_attribute::normal;
}