#include "call_token_extractor.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_set>

namespace tools
{

bool call_token_extractor::is_denylisted(std::string_view identifier)
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

std::vector<candidate_call_token> call_token_extractor::extract_candidates(const std::vector<std::string> &lines, int start_line)
{
	std::vector<candidate_call_token> result;
	std::unordered_set<std::string> seen;

	// Pattern to match identifier directly followed by opening parenthesis:
	// Matches `ident(` with optional whitespace before `(`.
	// Also matches member / scoped calls like `obj.ident(`, `obj->ident(`, `ns::ident(`.
	static const std::regex call_pattern(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*\()");

	for (size_t i = 0; i < lines.size(); ++i) {
		const std::string &line = lines[i];
		int line_num = start_line + static_cast<int>(i);

		// Skip comments and preprocessor lines
		size_t first_non_ws = line.find_first_not_of(" \t");
		if (first_non_ws == std::string::npos) {
			continue;
		}
		if (line[first_non_ws] == '#' || (line[first_non_ws] == '/' && first_non_ws + 1 < line.size() &&
						  (line[first_non_ws + 1] == '/' || line[first_non_ws + 1] == '*'))) {
			continue;
		}

		auto words_begin = std::sregex_iterator(line.begin(), line.end(), call_pattern);
		auto words_end = std::sregex_iterator();

		for (std::sregex_iterator it = words_begin; it != words_end; ++it) {
			const std::smatch &match = *it;
			if (match.size() > 1 && match[1].matched) {
				std::string candidate = match[1].str();
				if (!is_denylisted(candidate) && seen.find(candidate) == seen.end()) {
					seen.insert(candidate);
					int col = static_cast<int>(match.position(1));
					result.push_back({std::move(candidate), line_num, col});
					if (result.size() >= 20) {
						return result;
					}
				}
			}
		}
	}

	return result;
}

} // namespace tools
