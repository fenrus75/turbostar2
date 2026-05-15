#include "document.h"

document::document()
{
	lines_.emplace_back(""); // Start with one empty line
}

document::document(const std::string& filename)
	: filename_(filename)
{
	lines_.emplace_back(""); // Placeholder: Start with one empty line
	// TODO: load_from_file(filename_)
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
