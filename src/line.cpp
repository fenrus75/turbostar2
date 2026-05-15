#include "line.h"
#include <mutex>

line::line(const std::string& text)
	: text_(text)
{
}

line::line(const line& other)
{
	std::shared_lock lock(other.mutex_);
	text_ = other.text_;
}

line& line::operator=(const line& other)
{
	if (this != &other) {
		std::unique_lock lock_this(mutex_, std::defer_lock);
		std::shared_lock lock_other(other.mutex_, std::defer_lock);
		std::lock(lock_this, lock_other);
		text_ = other.text_;
	}
	return *this;
}

line::line(line&& other) noexcept
{
	std::unique_lock lock(other.mutex_);
	text_ = std::move(other.text_);
}

line& line::operator=(line&& other) noexcept
{
	if (this != &other) {
		std::unique_lock lock_this(mutex_, std::defer_lock);
		std::unique_lock lock_other(other.mutex_, std::defer_lock);
		std::lock(lock_this, lock_other);
		text_ = std::move(other.text_);
	}
	return *this;
}

std::string line::get_text() const
{
	std::shared_lock lock(mutex_);
	return text_;
}

void line::set_text(const std::string& text)
{
	std::unique_lock lock(mutex_);
	text_ = text;
}

size_t line::char_to_byte_offset(int char_pos) const
{
	size_t offset = 0;
	int chars = 0;
	while (chars < char_pos && offset < text_.length()) {
		unsigned char c = static_cast<unsigned char>(text_[offset]);
		if (c < 0x80) offset += 1;
		else if ((c & 0xE0) == 0xC0) offset += 2;
		else if ((c & 0xF0) == 0xE0) offset += 3;
		else if ((c & 0xF8) == 0xF0) offset += 4;
		else offset += 1; // Invalid UTF-8, skip 1
		chars++;
	}
	return offset;
}

size_t line::length_in_chars() const
{
	std::shared_lock lock(mutex_);
	size_t offset = 0;
	size_t chars = 0;
	while (offset < text_.length()) {
		unsigned char c = static_cast<unsigned char>(text_[offset]);
		if (c < 0x80) offset += 1;
		else if ((c & 0xE0) == 0xC0) offset += 2;
		else if ((c & 0xF0) == 0xE0) offset += 3;
		else if ((c & 0xF8) == 0xF0) offset += 4;
		else offset += 1;
		chars++;
	}
	return chars;
}

void line::insert_at(int char_pos, const std::string& utf8_char)
{
	std::unique_lock lock(mutex_);
	size_t offset = char_to_byte_offset(char_pos);
	if (offset <= text_.length()) {
		text_.insert(offset, utf8_char);
	}
}

void line::remove_at(int char_pos)
{
	std::unique_lock lock(mutex_);
	size_t offset = char_to_byte_offset(char_pos);
	if (offset < text_.length()) {
		size_t next_offset = offset;
		unsigned char c = static_cast<unsigned char>(text_[offset]);
		if (c < 0x80) next_offset += 1;
		else if ((c & 0xE0) == 0xC0) next_offset += 2;
		else if ((c & 0xF0) == 0xE0) next_offset += 3;
		else if ((c & 0xF8) == 0xF0) next_offset += 4;
		else next_offset += 1;
		
		text_.erase(offset, next_offset - offset);
	}
}

void line::split_at(int char_pos, line& new_line)
{
	std::unique_lock lock(mutex_);
	size_t offset = char_to_byte_offset(char_pos);
	if (offset <= text_.length()) {
		new_line.set_text(text_.substr(offset));
		text_.erase(offset);
	}
}

void line::merge(const line& other_line)
{
	std::unique_lock lock(mutex_);
	text_ += other_line.get_text();
}
