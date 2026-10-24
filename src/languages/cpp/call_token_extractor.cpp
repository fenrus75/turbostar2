#include "languages/cpp/call_token_extractor.h"
#include <algorithm>
#include <unordered_set>
#include "languages/cpp/keywords.h"
#include "languages/cpp/lexer.h"

namespace languages::cpp
{

std::vector<source_token> call_token_extractor::extract_candidates_from_source(std::string_view source, int start_line)
{
	std::vector<source_token> result;
	std::unordered_set<std::string> seen;

	auto tokens = lexer::tokenize(source);
	for (size_t i = 0; i < tokens.size(); ++i) {
		if (tokens[i].kind == token_kind::identifier) {
			std::string candidate(tokens[i].text);
			if (keywords::is_call_candidate_denylisted(candidate) || seen.find(candidate) != seen.end()) {
				continue;
			}

			// Look ahead for '(' skipping comments
			bool is_call = false;
			for (size_t j = i + 1; j < tokens.size(); ++j) {
				if (tokens[j].kind == token_kind::comment) {
					continue;
				}
				if (tokens[j].kind == token_kind::l_paren) {
					is_call = true;
				}
				break;
			}

			if (is_call) {
				seen.insert(candidate);
				int line_num = start_line + tokens[i].line - 1;
				int col = std::max(0, tokens[i].col - 1);
				result.push_back({std::move(candidate), line_num, col});
				if (result.size() >= 20) {
					return result;
				}
			}
		}
	}

	return result;
}

std::vector<source_token> call_token_extractor::extract_candidates(std::span<const std::string> lines, int start_line)
{
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
	return extract_candidates_from_source(source, start_line);
}

} // namespace languages::cpp
