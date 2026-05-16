#include "line.h"
#include <algorithm>
#include <mutex>

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

void line::set_text(const std::string &text)
{
	std::unique_lock lock(mutex_);
	text_ = text;
	lock.unlock();
	attributes_.assign(length_in_chars(), syntax_attribute::normal);
}

size_t line::char_to_byte_offset(int char_pos) const
{
	std::shared_lock lock(mutex_);
	size_t offset = 0;
	int chars = 0;
	while (chars < char_pos && offset < text_.length()) {
		unsigned char c = static_cast<unsigned char>(text_[offset]);
		if (c < 0x80)
			offset += 1;
		else if ((c & 0xE0) == 0xC0)
			offset += 2;
		else if ((c & 0xF0) == 0xE0)
			offset += 3;
		else if ((c & 0xF8) == 0xF0)
			offset += 4;
		else
			offset += 1; // Invalid UTF-8, skip 1
		chars++;
	}
	return offset;
}

int line::length_in_chars() const
{
	std::shared_lock lock(mutex_);
	int offset = 0;
	int chars = 0;
	while (offset < static_cast<int>(text_.length())) {
		unsigned char c = static_cast<unsigned char>(text_[offset]);
		if (c < 0x80)
			offset += 1;
		else if ((c & 0xE0) == 0xC0)
			offset += 2;
		else if ((c & 0xF0) == 0xE0)
			offset += 3;
		else if ((c & 0xF8) == 0xF0)
			offset += 4;
		else
			offset += 1;
		chars++;
	}
	return chars;
}

void line::insert_at(int char_pos, const std::string &utf8_char)
{
	std::unique_lock lock(mutex_);
	size_t offset = 0;
	int chars = 0;
	while (chars < char_pos && offset < text_.length()) {
		unsigned char c = static_cast<unsigned char>(text_[offset]);
		if (c < 0x80)
			offset += 1;
		else if ((c & 0xE0) == 0xC0)
			offset += 2;
		else if ((c & 0xF0) == 0xE0)
			offset += 3;
		else if ((c & 0xF8) == 0xF0)
			offset += 4;
		else
			offset += 1;
		chars++;
	}

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
	std::unique_lock lock(mutex_);
	size_t offset = 0;
	int chars = 0;
	while (chars < char_pos && offset < text_.length()) {
		unsigned char c = static_cast<unsigned char>(text_[offset]);
		if (c < 0x80)
			offset += 1;
		else if ((c & 0xE0) == 0xC0)
			offset += 2;
		else if ((c & 0xF0) == 0xE0)
			offset += 3;
		else if ((c & 0xF8) == 0xF0)
			offset += 4;
		else
			offset += 1;
		chars++;
	}

	if (offset < text_.length()) {
		size_t next_offset = offset;
		unsigned char c = static_cast<unsigned char>(text_[offset]);
		if (c < 0x80)
			next_offset += 1;
		else if ((c & 0xE0) == 0xC0)
			next_offset += 2;
		else if ((c & 0xF0) == 0xE0)
			next_offset += 3;
		else if ((c & 0xF8) == 0xF0)
			next_offset += 4;
		else
			next_offset += 1;

		text_.erase(offset, next_offset - offset);
		if (char_pos < static_cast<int>(attributes_.size())) {
			attributes_.erase(attributes_.begin() + char_pos);
		}
	}
}

void line::split_at(int char_pos, line &new_line)
{
	std::unique_lock lock(mutex_);
	size_t offset = 0;
	int chars = 0;
	while (chars < char_pos && offset < text_.length()) {
		unsigned char c = static_cast<unsigned char>(text_[offset]);
		if (c < 0x80)
			offset += 1;
		else if ((c & 0xE0) == 0xC0)
			offset += 2;
		else if ((c & 0xF0) == 0xE0)
			offset += 3;
		else if ((c & 0xF8) == 0xF0)
			offset += 4;
		else
			offset += 1;
		chars++;
	}

	if (offset <= text_.length()) {
		new_line.set_text(text_.substr(offset));
		text_.erase(offset);

		if (char_pos < static_cast<int>(attributes_.size())) {
			std::vector<syntax_attribute> new_attrs(attributes_.begin() + char_pos, attributes_.end());
			new_line.set_attributes(new_attrs);
			attributes_.erase(attributes_.begin() + char_pos, attributes_.end());
		}
	}
}

void line::merge(const line &other_line)
{
	std::unique_lock lock(mutex_);
	text_ += other_line.get_text();

	// Reset attributes for now, highlighter will redo it
	lock.unlock();
	attributes_.assign(length_in_chars(), syntax_attribute::normal);
}

int line::char_to_display_col(int char_pos) const
{
	std::shared_lock lock(mutex_);
	int col = 0;
	int current_char = 0;
	size_t byte_offset = 0;

	while (current_char < char_pos && byte_offset < text_.length()) {
		unsigned char c = static_cast<unsigned char>(text_[byte_offset]);
		int char_bytes = 1;

		if (c < 0x80) {
			if (c == '\t') {
				col = (col / 8 + 1) * 8;
			} else {
				col += 1;
			}
			char_bytes = 1;
		} else if ((c & 0xE0) == 0xC0)
			char_bytes = 2;
		else if ((c & 0xE0) == 0xE0)
			char_bytes = 3;
		else if ((c & 0xF0) == 0xF0)
			char_bytes = 4;
		else
			char_bytes = 1;

		if (c >= 0x80)
			col += 1; // Basic assumption: multi-byte chars are 1
				  // cell wide for now

		byte_offset += char_bytes;
		current_char++;
	}
	return col;
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
