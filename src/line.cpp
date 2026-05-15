#include "line.h"

line::line(const std::string& text)
	: text_(text)
{
}

const std::string& line::get_text() const
{
	return text_;
}

void line::set_text(const std::string& text)
{
	text_ = text;
}

void line::insert_at(int pos, char c)
{
	if (pos >= 0 && pos <= static_cast<int>(text_.length())) {
		text_.insert(pos, 1, c);
	}
}

void line::split_at(int pos, line& new_line)
{
	if (pos >= 0 && pos <= static_cast<int>(text_.length())) {
		new_line.set_text(text_.substr(pos));
		text_.erase(pos);
	}
}

void line::merge(const line& other_line)
{
	text_ += other_line.get_text();
}
