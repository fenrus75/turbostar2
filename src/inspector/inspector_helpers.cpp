#include "inspector/inspector_helpers.h"
#include <format>

namespace inspector
{

markdown_builder &markdown_builder::header(std::string_view label)
{
	buffer_ += std::format("**{}** ", label);
	return *this;
}

markdown_builder &markdown_builder::code(std::string_view text)
{
	buffer_ += std::format("`{}`", text);
	return *this;
}

markdown_builder &markdown_builder::bold(std::string_view text)
{
	buffer_ += std::format("**{}**", text);
	return *this;
}

markdown_builder &markdown_builder::italic(std::string_view text)
{
	buffer_ += std::format("*{}*", text);
	return *this;
}

markdown_builder &markdown_builder::text(std::string_view text)
{
	buffer_ += text;
	return *this;
}

markdown_builder &markdown_builder::numbered_arg(int index, std::string_view name)
{
	buffer_ += std::format("{}. `{}`", index, name);
	return *this;
}

markdown_builder &markdown_builder::bullet(std::string_view item)
{
	buffer_ += std::format("- {}", item);
	return *this;
}

markdown_builder &markdown_builder::link(std::string_view label, std::string_view target)
{
	buffer_ += std::format("[{}]({})", label, target);
	return *this;
}

markdown_builder &markdown_builder::newline()
{
	buffer_ += '\n';
	return *this;
}

std::string markdown_builder::build() const
{
	return buffer_;
}

std::string format_declaration_location(std::string_view file_path, int line)
{
	if (line > 0) {
		return std::format("[{}:{}]({})", file_path, line, file_path);
	}
	return std::format("[{}]({})", file_path, file_path);
}

} // namespace inspector
