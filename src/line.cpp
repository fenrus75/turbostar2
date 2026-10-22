#include "line.h"
#include <algorithm>
#include <mutex>
#include <string>
#include <string_view>
#include "utf8.h"

int line::global_tab_width = 8;

line::line(std::string_view text) : text_(text)
{
	// Initialize attributes to normal for all chars
	attributes_.assign(utf8::length(text_), syntax_attribute::normal);
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

void line::append_to(std::string &out) const
{
	std::shared_lock lock(mutex_);
	out.append(text_);
}

word_span line::get_word_span_at_unlocked(int char_pos, std::string_view extra) const
{
	int total_chars = static_cast<int>(attributes_.size());
	if (total_chars == 0 || char_pos < 0 || char_pos > total_chars) {
		return {};
	}

	// Reference position: if parked at EOL (char_pos == total_chars), test the preceding character
	int ref_pos = char_pos;
	if (ref_pos == total_chars) {
		ref_pos--;
	}

	size_t ref_byte = utf8::char_to_byte_offset(text_, ref_pos);
	if (ref_byte >= text_.size() || !utf8::is_word_char_at_byte(text_, ref_byte, extra)) {
		return {};
	}

	// Expand backward along UTF-8 character boundaries
	int start_char = ref_pos;
	size_t cur_byte = ref_byte;
	while (start_char > 0 && cur_byte > 0) {
		size_t prev_byte = cur_byte - 1;
		while (prev_byte > 0 && (static_cast<unsigned char>(text_[prev_byte]) & 0xC0) == 0x80) {
			prev_byte--;
		}
		if (!utf8::is_word_char_at_byte(text_, prev_byte, extra)) {
			break;
		}
		cur_byte = prev_byte;
		start_char--;
	}
	size_t start_byte = cur_byte;

	// Expand forward along UTF-8 character boundaries
	int end_char = ref_pos + 1;
	cur_byte = ref_byte + utf8::char_len(static_cast<unsigned char>(text_[ref_byte]));
	while (end_char < total_chars && cur_byte < text_.size()) {
		if (!utf8::is_word_char_at_byte(text_, cur_byte, extra)) {
			break;
		}
		cur_byte += utf8::char_len(static_cast<unsigned char>(text_[cur_byte]));
		end_char++;
	}
	size_t end_byte = cur_byte;

	return word_span{
	    .word = text_.substr(start_byte, end_byte - start_byte),
	    .start_char_pos = start_char,
	    .end_char_pos = end_char,
	    .kind = span_kind::word,
	};
}

word_span line::get_word_span_at(int char_pos, std::string_view extra) const
{
	std::shared_lock lock(mutex_);
	return get_word_span_at_unlocked(char_pos, extra);
}

std::string line::get_word_at(int char_pos, std::string_view extra) const
{
	return get_word_span_at(char_pos, extra).word;
}

word_span line::get_whitespace_span_at_unlocked(int char_pos) const
{
	int total_chars = static_cast<int>(attributes_.size());
	if (char_pos < 0 || char_pos >= total_chars) {
		return {};
	}

	size_t ref_byte = utf8::char_to_byte_offset(text_, char_pos);
	if (ref_byte >= text_.size() || !utf8::is_space_at_byte(text_, ref_byte)) {
		return {};
	}

	// Expand backward along UTF-8 character boundaries
	int start_char = char_pos;
	size_t cur_byte = ref_byte;
	while (start_char > 0 && cur_byte > 0) {
		size_t prev_byte = cur_byte - 1;
		while (prev_byte > 0 && (static_cast<unsigned char>(text_[prev_byte]) & 0xC0) == 0x80) {
			prev_byte--;
		}
		if (!utf8::is_space_at_byte(text_, prev_byte)) {
			break;
		}
		cur_byte = prev_byte;
		start_char--;
	}
	size_t start_byte = cur_byte;

	// Expand forward along UTF-8 character boundaries
	int end_char = char_pos + 1;
	cur_byte = ref_byte + utf8::char_len(static_cast<unsigned char>(text_[ref_byte]));
	while (end_char < total_chars && cur_byte < text_.size()) {
		if (!utf8::is_space_at_byte(text_, cur_byte)) {
			break;
		}
		cur_byte += utf8::char_len(static_cast<unsigned char>(text_[cur_byte]));
		end_char++;
	}
	size_t end_byte = cur_byte;

	return word_span{
	    .word = text_.substr(start_byte, end_byte - start_byte),
	    .start_char_pos = start_char,
	    .end_char_pos = end_char,
	    .kind = span_kind::whitespace,
	};
}

word_span line::get_whitespace_span_at(int char_pos) const
{
	std::shared_lock lock(mutex_);
	return get_whitespace_span_at_unlocked(char_pos);
}

word_span line::get_delimiter_span_at_unlocked(int char_pos) const
{
	int total_chars = static_cast<int>(attributes_.size());
	if (total_chars == 0 || char_pos < 0 || char_pos > total_chars) {
		return {};
	}

	// Reference position: if parked at EOL (char_pos == total_chars), test the preceding character
	int ref_pos = char_pos;
	if (ref_pos == total_chars) {
		ref_pos--;
	}

	size_t ref_byte = utf8::char_to_byte_offset(text_, ref_pos);
	if (ref_byte >= text_.size() || utf8::is_word_char_at_byte(text_, ref_byte) || utf8::is_space_at_byte(text_, ref_byte)) {
		return {};
	}

	size_t ref_len = utf8::char_len(static_cast<unsigned char>(text_[ref_byte]));
	std::string_view target_ch = std::string_view(text_).substr(ref_byte, ref_len);

	// Expand backward along UTF-8 character boundaries matching identical delimiter character
	int start_char = ref_pos;
	size_t cur_byte = ref_byte;
	while (start_char > 0 && cur_byte > 0) {
		size_t prev_byte = cur_byte - 1;
		while (prev_byte > 0 && (static_cast<unsigned char>(text_[prev_byte]) & 0xC0) == 0x80) {
			prev_byte--;
		}
		size_t prev_len = utf8::char_len(static_cast<unsigned char>(text_[prev_byte]));
		if (std::string_view(text_).substr(prev_byte, prev_len) != target_ch) {
			break;
		}
		cur_byte = prev_byte;
		start_char--;
	}
	size_t start_byte = cur_byte;

	// Expand forward along UTF-8 character boundaries matching identical delimiter character
	int end_char = ref_pos + 1;
	cur_byte = ref_byte + ref_len;
	while (end_char < total_chars && cur_byte < text_.size()) {
		size_t next_len = utf8::char_len(static_cast<unsigned char>(text_[cur_byte]));
		if (std::string_view(text_).substr(cur_byte, next_len) != target_ch) {
			break;
		}
		cur_byte += next_len;
		end_char++;
	}
	size_t end_byte = cur_byte;

	return word_span{
	    .word = text_.substr(start_byte, end_byte - start_byte),
	    .start_char_pos = start_char,
	    .end_char_pos = end_char,
	    .kind = span_kind::delimiter,
	};
}

word_span line::get_delimiter_span_at(int char_pos) const
{
	std::shared_lock lock(mutex_);
	return get_delimiter_span_at_unlocked(char_pos);
}

std::string line::get_delimiter_at(int char_pos) const
{
	return get_delimiter_span_at(char_pos).word;
}

word_span line::get_element_span_at_unlocked(int char_pos, std::string_view extra, std::string_view scope_delimiter) const
{
	int total_chars = static_cast<int>(attributes_.size());
	if (total_chars == 0 || char_pos < 0 || char_pos > total_chars) {
		return {};
	}

	int ref_pos = char_pos;
	if (ref_pos == total_chars) {
		ref_pos--;
	}

	size_t ref_byte = utf8::char_to_byte_offset(text_, ref_pos);
	word_span base_span;
	if (utf8::is_space_at_byte(text_, ref_byte)) {
		base_span = get_whitespace_span_at_unlocked(char_pos);
	} else if (utf8::is_word_char_at_byte(text_, ref_byte, extra)) {
		base_span = get_word_span_at_unlocked(char_pos, extra);
	} else {
		base_span = get_delimiter_span_at_unlocked(char_pos);
	}

	if (scope_delimiter.empty() || base_span.empty() || base_span.kind == span_kind::whitespace) {
		return base_span;
	}

	if (base_span.kind == span_kind::delimiter && base_span.word != scope_delimiter) {
		return base_span;
	}

	int current_start_char = base_span.start_char_pos;
	int current_end_char = base_span.end_char_pos;

	// If starting on the scope delimiter itself, incorporate adjacent words first
	if (base_span.kind == span_kind::delimiter) {
		if (current_start_char > 0) {
			auto prev_word = get_word_span_at_unlocked(current_start_char - 1, extra);
			if (!prev_word.empty() && prev_word.end_char_pos == current_start_char) {
				current_start_char = prev_word.start_char_pos;
			}
		}
		if (current_end_char < total_chars) {
			auto next_word = get_word_span_at_unlocked(current_end_char, extra);
			if (!next_word.empty() && next_word.start_char_pos == current_end_char) {
				current_end_char = next_word.end_char_pos;
			}
		}
	}

	// Backward expansion across alternating scope_delimiter and words
	while (current_start_char > 0) {
		int prev_pos = current_start_char - 1;
		auto delim_span = get_delimiter_span_at_unlocked(prev_pos);
		if (delim_span.empty() || delim_span.word != scope_delimiter || delim_span.end_char_pos != current_start_char) {
			break;
		}

		current_start_char = delim_span.start_char_pos;
		if (current_start_char == 0) {
			break;
		}

		auto prev_word = get_word_span_at_unlocked(current_start_char - 1, extra);
		if (prev_word.empty() || prev_word.end_char_pos != current_start_char) {
			break;
		}
		current_start_char = prev_word.start_char_pos;
	}

	// Forward expansion across alternating scope_delimiter and words
	while (current_end_char < total_chars) {
		auto delim_span = get_delimiter_span_at_unlocked(current_end_char);
		if (delim_span.empty() || delim_span.word != scope_delimiter || delim_span.start_char_pos != current_end_char) {
			break;
		}

		current_end_char = delim_span.end_char_pos;
		if (current_end_char >= total_chars) {
			break;
		}

		auto next_word = get_word_span_at_unlocked(current_end_char, extra);
		if (!next_word.empty() && next_word.start_char_pos == current_end_char) {
			current_end_char = next_word.end_char_pos;
		} else {
			break;
		}
	}

	if (current_start_char == base_span.start_char_pos && current_end_char == base_span.end_char_pos) {
		return base_span;
	}

	size_t start_byte = utf8::char_to_byte_offset(text_, current_start_char);
	size_t end_byte = (current_end_char >= total_chars) ? text_.size() : utf8::char_to_byte_offset(text_, current_end_char);

	return word_span{
	    .word = text_.substr(start_byte, end_byte - start_byte),
	    .start_char_pos = current_start_char,
	    .end_char_pos = current_end_char,
	    .kind = span_kind::word,
	};
}

word_span line::get_element_span_at(int char_pos, std::string_view extra, std::string_view scope_delimiter) const
{
	std::shared_lock lock(mutex_);
	return get_element_span_at_unlocked(char_pos, extra, scope_delimiter);
}

std::string line::get_element_at(int char_pos, std::string_view extra, std::string_view scope_delimiter) const
{
	return get_element_span_at(char_pos, extra, scope_delimiter).word;
}

void line::get_content(std::string &out_text, std::vector<syntax_attribute> &out_attrs) const
{
	std::shared_lock lock(mutex_);
	out_text = text_;
	out_attrs = attributes_;
}

void line::set_text(std::string_view text)
{
	std::unique_lock lock(mutex_);
	text_ = std::string(text);
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

size_t line::length_in_bytes() const
{
	std::shared_lock lock(mutex_);
	return text_.size();
}

bool line::next_utf8_character(size_t &byte_offset, std::string &out_char) const
{
	std::shared_lock lock(mutex_);
	return utf8::next_character(text_, byte_offset, out_char);
}

void line::insert_at(int char_pos, std::string_view utf8_char)
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
		attributes_.insert(attributes_.begin() + char_pos, syntax_attribute::normal);
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
		size_t next_offset = offset + utf8::char_len(byte_at_unlocked(static_cast<int>(offset)));
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
	if (this == &other_line) {
		return;
	}
	std::unique_lock lock_this(mutex_, std::defer_lock);
	std::shared_lock lock_other(other_line.mutex_, std::defer_lock);
	std::lock(lock_this, lock_other);

	text_ += other_line.text_;
	attributes_.insert(attributes_.end(), other_line.attributes_.begin(), other_line.attributes_.end());
}

int line::char_to_display_col_unlocked(int char_pos) const
{
	int col = 0;
	int current_char = 0;
	size_t byte_offset = 0;

	while (current_char < char_pos && byte_offset < text_.length()) {
		unsigned char c = byte_at_unlocked(static_cast<int>(byte_offset));
		size_t char_bytes = utf8::char_len(c);
		if (byte_offset + char_bytes > text_.length()) {
			char_bytes = text_.length() - byte_offset;
		}

		std::string_view glyph(text_.data() + byte_offset, char_bytes);

		if (c < 0x80) {
			if (c == '\t') {
				col = (col / global_tab_width + 1) * global_tab_width;
			} else {
				col += 1;
			}
		} else {
			col += static_cast<int>(utf8::display_width(glyph));
		}

		byte_offset += char_bytes;
		current_char++;
	}
	return col;
}

int line::char_to_display_col(int char_pos) const
{
	std::shared_lock lock(mutex_);
	return char_to_display_col_unlocked(char_pos);
}

int line::display_col_to_char_pos(int display_col) const
{
	std::shared_lock lock(mutex_);
	int len = utf8::length(text_);
	int best_char_pos = 0;
	int best_diff = 999999;
	for (int pos = 0; pos <= len; ++pos) {
		int col = char_to_display_col_unlocked(pos);
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

unsigned char line::byte_at_unlocked(int offset) const noexcept
{
	if (offset >= 0 && offset < static_cast<int>(text_.length())) {
		return static_cast<unsigned char>(text_[offset]);
	}
	return 0;
}

bool line::is_space_at(int char_pos) const
{
	if (char_pos < 0) {
		return false;
	}
	std::shared_lock lock(mutex_);
	return utf8::is_space_at_char(text_, static_cast<size_t>(char_pos));
}

bool line::is_delimiter_at(int char_pos) const
{
	if (char_pos < 0) {
		return false;
	}
	std::shared_lock lock(mutex_);
	return utf8::is_delimiter_at_char(text_, static_cast<size_t>(char_pos));
}

bool line::is_terminator_at(int char_pos) const
{
	return is_delimiter_at(char_pos);
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