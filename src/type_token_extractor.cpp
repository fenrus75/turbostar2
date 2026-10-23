#include "type_token_extractor.h"
#include "languages/cpp/language.h"
#include "languages/cpp/type_token_extractor.h"

namespace tools
{

std::vector<source_token> type_token_extractor::extract_candidates(std::span<const std::string> lines, int start_line)
{
	return languages::cpp::type_token_extractor::extract_candidates(lines, start_line);
}

std::vector<source_token> type_token_extractor::extract_candidates(std::string_view path, std::span<const std::string> lines, int start_line)
{
	if (!languages::cpp::supports_file(path)) {
		return {};
	}
	return languages::cpp::type_token_extractor::extract_candidates(lines, start_line);
}

} // namespace tools
