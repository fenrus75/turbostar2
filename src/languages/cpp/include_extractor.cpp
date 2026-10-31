#include "languages/cpp/include_extractor.h"
#include <fstream>
#include <ctre.hpp>
#include "languages/cpp/lexer.h"

namespace languages::cpp
{

std::vector<include_info> include_extractor::extract_includes(std::span<const std::string> lines, int start_line)
{
	std::vector<include_info> result;

	std::string source;
	size_t total_size = 0;
	for (const auto &l : lines) {
		total_size += l.size() + 1;
	}
	source.reserve(total_size);
	for (const auto &l : lines) {
		source += l;
		source += '\n';
	}

	auto tokens = lexer::tokenize(source);
	for (const auto &tok : tokens) {
		if (tok.kind == token_kind::preprocessor) {
			if (auto m = ctre::search<R"(^#\s*include\s*([<"])([^>"]+)[>"])">(tok.text)) {
				bool is_sys = (m.get<1>().to_view() == "<");
				result.push_back({std::string(m.get<2>().to_view()), is_sys, start_line + tok.line - 1});
			}
		}
	}

	return result;
}

std::vector<include_info> include_extractor::extract_includes_from_file(const std::string &filepath, size_t max_lines)
{
	std::ifstream file(filepath);
	if (!file.is_open()) {
		return {};
	}

	std::string source;
	std::string line;
	size_t line_count = 0;
	while (line_count < max_lines && std::getline(file, line)) {
		source += line;
		source += '\n';
		++line_count;
	}

	std::vector<include_info> result;

	auto tokens = lexer::tokenize(source);
	for (const auto &tok : tokens) {
		if (tok.kind == token_kind::preprocessor) {
			if (auto m = ctre::search<R"(^#\s*include\s*([<"])([^>"]+)[>"])">(tok.text)) {
				bool is_sys = (m.get<1>().to_view() == "<");
				result.push_back({std::string(m.get<2>().to_view()), is_sys, tok.line});
			}
		}
	}

	return result;
}

std::unordered_set<std::string> include_extractor::extract_header_names(std::span<const std::string> lines)
{
	std::unordered_set<std::string> headers;
	auto incs = extract_includes(lines);
	for (auto &inc : incs) {
		headers.insert(std::move(inc.path));
	}
	return headers;
}

std::unordered_set<std::string> include_extractor::extract_header_names_from_file(const std::string &filepath, size_t max_lines)
{
	std::unordered_set<std::string> headers;
	auto incs = extract_includes_from_file(filepath, max_lines);
	for (auto &inc : incs) {
		headers.insert(std::move(inc.path));
	}
	return headers;
}

} // namespace languages::cpp