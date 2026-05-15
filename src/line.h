#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <shared_mutex>

class line {
public:
	line() = default;
	explicit line(const std::string& text);
	~line() = default;

	// Custom copy/move to handle mutex
	line(const line& other);
	line& operator=(const line& other);
	line(line&& other) noexcept;
	line& operator=(line&& other) noexcept;

	std::string get_text() const;
	void set_text(const std::string& text);

	void insert_at(int char_pos, const std::string& utf8_char);
	void remove_at(int char_pos);
	void split_at(int char_pos, line& new_line);
	void merge(const line& other_line);

	size_t length_in_chars() const;
	size_t char_to_byte_offset(int char_pos) const;

private:
	std::string text_;
	mutable std::shared_mutex mutex_;
	// Future: Metadata for syntax highlighting, e.g., std::vector<uint32_t> attributes_;
};
