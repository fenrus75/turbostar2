#include "type_token_extractor.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_set>

namespace tools
{

bool type_token_extractor::is_denylisted(std::string_view identifier)
{
	static const std::unordered_set<std::string_view> denylist = {
	    // C/C++ primitives & keywords
	    "auto", "bool", "char", "char8_t", "char16_t", "char32_t", "wchar_t", "int", "short", "long", "float", "double", "void",
	    "size_t", "ssize_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t", "const",
	    "constexpr", "consteval", "static", "inline", "extern", "virtual", "override", "final", "explicit", "mutable", "register",
	    "volatile", "unsigned", "signed", "class", "struct", "enum", "union", "namespace", "typename", "template", "public",
	    "protected", "private", "typedef", "using", "friend", "operator", "return", "if", "else", "for", "while", "do", "switch",
	    "case", "default", "break", "continue", "goto", "try", "catch", "throw", "noexcept", "nullptr", "true", "false", "sizeof",
	    "alignof", "decltype", "static_assert", "typeid", "std", "this",

	    // Standard library containers & utilities
	    "string", "string_view", "vector", "list", "deque", "array", "set", "unordered_set", "multiset", "unordered_multiset", "map",
	    "unordered_map", "multimap", "unordered_multimap", "pair", "tuple", "optional", "variant", "any", "unique_ptr", "shared_ptr",
	    "weak_ptr", "span", "function", "atomic", "mutex", "shared_mutex", "lock_guard", "unique_lock", "shared_lock", "scoped_lock",
	    "thread", "jthread", "condition_variable", "filesystem", "path", "ifstream", "ofstream", "fstream", "stringstream",
	    "istringstream", "ostringstream", "iostream", "istream", "ostream", "cin", "cout", "cerr", "endl", "format", "format_to",
	    "regex", "smatch", "sregex_iterator", "chrono", "steady_clock", "system_clock", "time_point", "duration", "milliseconds",
	    "seconds", "nanoseconds", "microsecond",

	    // Common third-party / external namespaces
	    "nlohmann", "json", "basic_json", "fmt", "re2", "RE2", "sqlite3", "sqlite3_stmt"};

	if (identifier.length() < 3) {
		return true;
	}

	// Filter out ALL_CAPS macros
	bool all_upper = true;
	for (char c : identifier) {
		if (std::islower(static_cast<unsigned char>(c))) {
			all_upper = false;
			break;
		}
	}
	if (all_upper) {
		return true;
	}

	return denylist.find(identifier) != denylist.end();
}

std::vector<candidate_type_token> type_token_extractor::extract_candidates(const std::vector<std::string> &lines, int start_line)
{
	std::vector<candidate_type_token> result;
	std::unordered_set<std::string> seen;

	// Regex to match potential type indicators:
	// 1. Qualified prefix or type followed by :: (e.g. MyClass::method, MyStruct::field)
	// 2. Type in pointer/reference param (e.g. MyType *ptr, const MyType &ref)
	// 3. Template argument (e.g. <MyType>)
	// 4. Type followed by identifier (e.g. MyContext ctx, Widget widget)
	static const std::regex type_patterns(
	    R"((?:class|struct|enum)\s+([A-Za-z_][A-Za-z0-9_]*)|)"
	    R"(\b([A-Za-z_][A-Za-z0-9_]*)::|)"
	    R"(<(?:\s*const\s+)?([A-Za-z_][A-Za-z0-9_]*)(?:\s*[*&])?\s*>|)"
	    R"(\b([A-Za-z_][A-Za-z0-9_]*)\s+(?:const\s+)?(?:\*|&)+\s*[A-Za-z_]|)"
	    R"(\b(?:const\s+)?([A-Za-z_][A-Za-z0-9_]*)\s+(?:\*|&)*\s*[A-Za-z_][A-Za-z0-9_]*\s*[\(\{\;\,])");

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

		auto words_begin = std::sregex_iterator(line.begin(), line.end(), type_patterns);
		auto words_end = std::sregex_iterator();

		for (std::sregex_iterator it = words_begin; it != words_end; ++it) {
			const std::smatch &match = *it;
			for (size_t g = 1; g < match.size(); ++g) {
				if (match[g].matched) {
					std::string candidate = match[g].str();
					if (!is_denylisted(candidate) && seen.find(candidate) == seen.end()) {
						seen.insert(candidate);
						int col = static_cast<int>(match.position(g));
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

} // namespace tools
