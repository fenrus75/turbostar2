#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace inspector
{

/**
 * @brief Fluent builder for constructing Markdown Lite output for Symbol Inspector providers.
 */
class markdown_builder
{
      public:
	markdown_builder() = default;

	markdown_builder &header(std::string_view label);
	markdown_builder &code(std::string_view text);
	markdown_builder &bold(std::string_view text);
	markdown_builder &italic(std::string_view text);
	markdown_builder &text(std::string_view text);
	markdown_builder &numbered_arg(int index, std::string_view name);
	markdown_builder &bullet(std::string_view item);
	markdown_builder &link(std::string_view label, std::string_view target);
	markdown_builder &newline();

	std::string build() const;

      private:
	std::string buffer_;
};

/**
 * @brief Helper to format a declaration location string as a markdown link or code snippet.
 */
std::string format_declaration_location(std::string_view file_path, int line = 0);

} // namespace inspector
