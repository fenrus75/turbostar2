#pragma once

#include <string>
#include <vector>
#include <shared_mutex>
#include "line.h"

class document {
public:
	document();
	explicit document(const std::string& filename);
	~document() = default;

	bool load_from_file(const std::string& filename);
	const std::string& get_filename() const;
	bool is_modified() const;
	
	// Basic accessors for now
	size_t get_line_count() const;
	const line& get_line(size_t index) const;

	int get_cursor_x() const;
	int get_cursor_y() const;
	void move_cursor(int dx, int dy);

private:
	std::vector<line> lines_;
	mutable std::shared_mutex mutex_;
	
	std::string filename_;
	bool modified_{false};
	
	int cursor_x_{0};
	int cursor_y_{0};
	
	int selection_start_x_{-1};
	int selection_start_y_{-1};
	int selection_end_x_{-1};
	int selection_end_y_{-1};
};
