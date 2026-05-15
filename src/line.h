#pragma once

#include <string>
#include <vector>
#include <cstdint>

class line {
public:
	line() = default;
	explicit line(const std::string& text);
	~line() = default;

	const std::string& get_text() const;
	void set_text(const std::string& text);

	void insert_at(int pos, char c);
	void split_at(int pos, line& new_line);
	void merge(const line& other_line);

private:
	std::string text_;
	// Future: Metadata for syntax highlighting, e.g., std::vector<uint32_t> attributes_;
};
