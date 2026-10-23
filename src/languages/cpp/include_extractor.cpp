#include "languages/cpp/include_extractor.h"
#include <fstream>
#include <re2/re2.h>
#include "languages/cpp/source_masker.h"

namespace languages::cpp
{

std::vector<include_info> include_extractor::extract_includes(std::span<const std::string> lines, int start_line)
{
	std::vector<include_info> result;
	static const re2::RE2 inc_regex(R"(^\s*#\s*include\s*([<"])([^>"]+)[>"])");

	std::vector<std::string> masked = source_masker::mask_lines(lines);
	for (size_t i = 0; i < masked.size(); ++i) {
		std::string delim, header;
		if (re2::RE2::PartialMatch(masked[i], inc_regex, &delim, &header)) {
			bool is_sys = (delim == "<");
			result.push_back({std::move(header), is_sys, start_line + static_cast<int>(i)});
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

	std::vector<std::string> lines;
	std::string line;
	while (std::getline(file, line) && lines.size() < max_lines) {
		lines.push_back(std::move(line));
	}

	return extract_includes(lines, 1);
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
