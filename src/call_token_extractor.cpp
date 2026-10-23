#include "call_token_extractor.h"
#include "languages/cpp/call_token_extractor.h"
#include "languages/cpp/language.h"

namespace tools
{

std::vector<source_token> call_token_extractor::extract_candidates(std::span<const std::string> lines, int start_line)
{
	return languages::cpp::call_token_extractor::extract_candidates(lines, start_line);
}

std::vector<source_token> call_token_extractor::extract_candidates(std::string_view path, std::span<const std::string> lines, int start_line)
{
	if (!languages::cpp::supports_file(path)) {
		return {};
	}
	return languages::cpp::call_token_extractor::extract_candidates(lines, start_line);
}

} // namespace tools
