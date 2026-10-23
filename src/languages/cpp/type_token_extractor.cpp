#include "languages/cpp/type_token_extractor.h"
#include <algorithm>
#include <re2/re2.h>
#include <unordered_set>
#include "languages/cpp/keywords.h"
#include "languages/cpp/source_masker.h"

namespace languages::cpp
{

std::vector<source_token> type_token_extractor::extract_candidates(std::span<const std::string> lines, int start_line)
{
	std::vector<source_token> result;
	std::unordered_set<std::string> seen;

	// Regex to match potential type indicators:
	// 1. Qualified prefix or type followed by :: (e.g. MyClass::method, MyStruct::field)
	// 2. Type in pointer/reference param (e.g. MyType *ptr, const MyType &ref)
	// 3. Template argument (e.g. <MyType>)
	// 4. Type followed by identifier (e.g. MyContext ctx, Widget widget)
	static const re2::RE2 type_patterns(
	    R"((?:class|struct|enum)\s+([A-Za-z_][A-Za-z0-9_]*)|)"
	    R"(\b([A-Za-z_][A-Za-z0-9_]*)::|)"
	    R"(<\s*(?:const\s+)?([A-Za-z_][A-Za-z0-9_]*)(?:\s*[*&])?\s*>|)"
	    R"(\b([A-Za-z_][A-Za-z0-9_]*)\s+(?:const\s+)?(?:\*|&)+\s*[A-Za-z_]|)"
	    R"(\b(?:const\s+)?([A-Za-z_][A-Za-z0-9_]*)\s+(?:\*|&)*\s*[A-Za-z_][A-Za-z0-9_]*\s*[\(\)\{\;\,])");

	std::vector<std::string> masked_lines = source_masker::mask_lines(lines);

	for (size_t i = 0; i < masked_lines.size(); ++i) {
		const std::string &line = masked_lines[i];
		int line_num = start_line + static_cast<int>(i);

		// Skip comments (already masked to spaces) and preprocessor lines
		size_t first_non_ws = line.find_first_not_of(" \t");
		if (first_non_ws == std::string::npos) {
			continue;
		}
		if (line[first_non_ws] == '#') {
			continue;
		}

		size_t pos = 0;
		re2::StringPiece submatches[6];
		while (pos < line.size() && type_patterns.Match(line, pos, line.size(), re2::RE2::UNANCHORED, submatches, 6)) {
			pos = static_cast<size_t>(submatches[0].data() - line.data()) + std::max<size_t>(1, submatches[0].size());
			for (size_t g = 1; g < 6; ++g) {
				if (submatches[g].data() != nullptr) {
					std::string candidate(submatches[g].data(), submatches[g].size());
					if (!keywords::is_type_candidate_denylisted(candidate) && seen.find(candidate) == seen.end()) {
						seen.insert(candidate);
						int col = static_cast<int>(submatches[g].data() - line.data());
						result.push_back({candidate, line_num, col});
						if (result.size() >= 15) {
							return result;
						}
					}
				}
			}
		}
	}

	return result;
}

} // namespace languages::cpp
