#pragma once

#include <cstdint>
#include <shared_mutex>
#include <string>
#include <vector>
/**
 * @brief Categorization of characters for syntax highlighting.
 */
enum class syntax_attribute : uint8_t { normal = 0, keyword = 1, comment = 2, string_literal = 3 };

class line
{
      public:
	line() = default;
	explicit line(const std::string &text);
	~line() = default;

	// Custom copy/move to handle mutex
	line(const line &other);
	line &operator=(const line &other);
	line(line &&other) noexcept;
	line &operator=(line &&other) noexcept;

	std::string get_text() const;
	void set_text(const std::string &text);

	void insert_at(int char_pos, const std::string &utf8_char);
	void remove_at(int char_pos);
	void split_at(int char_pos, line &new_line);
	void merge(const line &other_line);

	size_t length_in_chars() const;
	size_t char_to_byte_offset(int char_pos) const;
	int char_to_display_col(int char_pos) const;

	// Syntax highlighting
	void set_attributes(const std::vector<syntax_attribute> &attrs);
	syntax_attribute get_attribute(int char_pos) const;

      private:
	std::string text_;
	std::vector<syntax_attribute> attributes_;
	mutable std::shared_mutex mutex_;
};
