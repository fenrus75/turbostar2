#include "languages/cpp/call_token_extractor.h"
#include <algorithm>
#include <cctype>
#include <re2/re2.h>
#include <unordered_set>
#include "languages/cpp/source_masker.h"

namespace languages::cpp
{

namespace
{

bool is_denylisted(std::string_view identifier)
{
	static const std::unordered_set<std::string_view> denylist = {
	    // C/C++ control flow keywords
	    "if", "else", "while", "for", "do", "switch", "case", "default", "return", "goto",
	    "break", "continue", "try", "catch", "throw",

	    // Operators, casts & type queries
	    "sizeof", "typeof", "__typeof__", "__typeof", "alignof", "decltype", "static_assert",
	    "static_cast", "dynamic_cast", "const_cast", "reinterpret_cast", "typeid", "alignas",
	    "new", "delete", "defined", "__attribute__", "__builtin_expect",

	    // Common non-call macro annotations
	    "likely", "unlikely", "offsetof", "container_of",

	    // Common keywords
	    "auto", "bool", "char", "int", "short", "long", "float", "double", "void", "unsigned",
	    "signed", "class", "struct", "enum", "union", "namespace", "typename", "template",
	    "public", "protected", "private", "typedef", "using", "friend", "operator", "const",
	    "constexpr", "consteval", "static", "inline", "extern", "virtual", "override", "final",
	    "explicit", "mutable", "register", "volatile", "noexcept", "nullptr", "true", "false",
	    "this"
	};

	if (identifier.length() < 2) {
		return true;
	}

	return denylist.find(identifier) != denylist.end();
}

} // anonymous namespace

std::vector<source_token> call_token_extractor::extract_candidates(std::span<const std::string> lines, int start_line)
{
	std::vector<source_token> result;
	std::unordered_set<std::string> seen;

	// Pattern to match identifier directly followed by opening parenthesis:
	// Matches `ident(` with optional whitespace before `(`.
	// Also matches member / scoped calls like `obj.ident(`, `obj->ident(`, `ns::ident(`.
	static const re2::RE2 call_pattern(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*\()");

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
		re2::StringPiece submatches[2];
		while (pos < line.size() && call_pattern.Match(line, pos, line.size(), re2::RE2::UNANCHORED, submatches, 2)) {
			std::string candidate(submatches[1].data(), submatches[1].size());
			int col = static_cast<int>(submatches[1].data() - line.data());
			pos = static_cast<size_t>(submatches[0].data() - line.data()) + submatches[0].size();
			if (!is_denylisted(candidate) && seen.find(candidate) == seen.end()) {
				seen.insert(candidate);
				result.push_back({std::move(candidate), line_num, col});
				if (result.size() >= 20) {
					return result;
				}
			}
		}
	}

	return result;
}

} // namespace languages::cpp
