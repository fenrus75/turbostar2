#include "languages/cpp/keywords.h"
#include <cctype>
#include <unordered_set>

namespace languages::cpp
{

namespace
{

const std::unordered_set<std::string_view> &get_keywords_set()
{
	static const std::unordered_set<std::string_view> kws = {
	    // Control flow
	    "if", "else", "switch", "case", "default", "for", "while", "do", "break", "continue",
	    "goto", "return", "try", "catch", "throw", "co_await", "co_return", "co_yield",

	    // Types & declarations
	    "auto", "bool", "char", "char8_t", "char16_t", "char32_t", "wchar_t", "int", "short",
	    "long", "float", "double", "void", "signed", "unsigned", "class", "struct", "union",
	    "enum", "namespace", "concept", "requires", "template", "typename", "typedef", "using",
	    "friend",

	    // Modifiers & specifiers
	    "const", "constexpr", "consteval", "constinit", "volatile", "mutable", "static", "inline",
	    "extern", "virtual", "override", "final", "explicit", "noexcept", "register", "export",
	    "import", "module",

	    // Operators, queries & memory
	    "sizeof", "decltype", "alignof", "alignas", "typeid", "new", "delete", "static_assert",
	    "static_cast", "dynamic_cast", "const_cast", "reinterpret_cast", "this", "nullptr",
	    "true", "false", "operator",

	    // Alternative operator tokens
	    "and", "and_eq", "bitand", "bitor", "compl", "not", "not_eq", "or", "or_eq", "xor", "xor_eq",

	    // Common GCC / Clang builtins & extensions
	    "typeof", "__typeof__", "__typeof", "asm", "__asm__", "__attribute__", "__builtin_expect"
	};
	return kws;
}

const std::unordered_set<std::string_view> &get_primitive_types_set()
{
	static const std::unordered_set<std::string_view> primitives = {
	    // Fundamental types & qualifiers
	    "auto", "void", "bool", "char", "wchar_t", "char8_t", "char16_t", "char32_t",
	    "short", "int", "long", "float", "double", "signed", "unsigned",

	    // Standard integer typedefs (<cstdint>, <cstddef>)
	    "int8_t", "int16_t", "int32_t", "int64_t",
	    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
	    "int_least8_t", "int_least16_t", "int_least32_t", "int_least64_t",
	    "uint_least8_t", "uint_least16_t", "uint_least32_t", "uint_least64_t",
	    "int_fast8_t", "int_fast16_t", "int_fast32_t", "int_fast64_t",
	    "uint_fast8_t", "uint_fast16_t", "uint_fast32_t", "uint_fast64_t",
	    "intptr_t", "uintptr_t", "ptrdiff_t", "size_t", "ssize_t", "intmax_t", "uintmax_t",
	    "nullptr_t", "max_align_t",

	    // Linux kernel / embedded fixed types
	    "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "ktime_t"
	};
	return primitives;
}

const std::unordered_set<std::string_view> &get_function_specifiers_set()
{
	static const std::unordered_set<std::string_view> specifiers = {
	    // Storage, linkage, template & evaluation specifiers
	    "static", "inline", "virtual", "explicit", "extern", "constexpr", "consteval", "constinit",
	    "noexcept", "volatile", "mutable", "const", "unsigned", "signed",
	    "template", "typename", "concept", "requires",

	    // Kernel & compiler annotations
	    "noinstr", "__always_inline", "__init", "__exit", "asmlinkage", "__sched", "__attribute__",

	    // Fundamental & standard types (can appear as return type on specifier lines)
	    "auto", "void", "bool", "char", "wchar_t", "char8_t", "char16_t", "char32_t",
	    "short", "int", "long", "float", "double",
	    "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
	    "int8_t", "int16_t", "int32_t", "int64_t",
	    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
	    "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "ktime_t"
	};
	return specifiers;
}

const std::unordered_set<std::string_view> &get_std_types_set()
{
	static const std::unordered_set<std::string_view> std_types = {
	    // Core std namespace and string utilities
	    "std", "string", "string_view", "wstring", "u8string", "u16string", "u32string",

	    // Containers and sequence views
	    "vector", "list", "forward_list", "deque", "array", "span", "queue", "priority_queue", "stack",
	    "set", "unordered_set", "multiset", "unordered_multiset",
	    "map", "unordered_map", "multimap", "unordered_multimap",

	    // Vocabulary types & smart pointers
	    "pair", "tuple", "optional", "variant", "any",
	    "unique_ptr", "shared_ptr", "weak_ptr", "allocator", "initializer_list",

	    // Function objects & concurrency
	    "function", "atomic", "atomic_flag", "mutex", "shared_mutex", "recursive_mutex",
	    "lock_guard", "unique_lock", "shared_lock", "scoped_lock",
	    "thread", "jthread", "condition_variable", "condition_variable_any",

	    // I/O & filesystem
	    "filesystem", "path", "ifstream", "ofstream", "fstream", "stringstream",
	    "istringstream", "ostringstream", "iostream", "istream", "ostream",
	    "cin", "cout", "cerr", "endl",

	    // Formatting & regex
	    "format", "format_to", "format_args", "regex", "smatch", "cmatch", "sregex_iterator",

	    // Chrono
	    "chrono", "steady_clock", "system_clock", "high_resolution_clock",
	    "time_point", "duration", "hours", "minutes", "seconds", "milliseconds",
	    "microseconds", "nanoseconds"
	};
	return std_types;
}

const std::unordered_set<std::string_view> &get_noise_namespaces_set()
{
	static const std::unordered_set<std::string_view> noise = {
	    "nlohmann", "json", "basic_json", "ordered_json",
	    "fmt", "re2", "RE2", "sqlite3", "sqlite3_stmt"
	};
	return noise;
}

const std::unordered_set<std::string_view> &get_call_denylist_set()
{
	static const std::unordered_set<std::string_view> denylist = {
	    // Control flow keywords
	    "if", "else", "while", "for", "do", "switch", "case", "default", "return", "goto",
	    "break", "continue", "try", "catch", "throw", "co_await", "co_return", "co_yield",

	    // Operators, casts & type queries
	    "sizeof", "typeof", "__typeof__", "__typeof", "alignof", "alignas", "decltype",
	    "static_assert", "static_cast", "dynamic_cast", "const_cast", "reinterpret_cast",
	    "typeid", "new", "delete", "defined", "__attribute__", "__builtin_expect",
	    "concept", "requires",

	    // Common non-call macro annotations
	    "likely", "unlikely", "offsetof", "container_of",

	    // Common keywords & specifiers
	    "auto", "bool", "char", "int", "short", "long", "float", "double", "void", "unsigned",
	    "signed", "class", "struct", "enum", "union", "namespace", "typename", "template",
	    "public", "protected", "private", "typedef", "using", "friend", "operator", "const",
	    "constexpr", "consteval", "constinit", "static", "inline", "extern", "virtual",
	    "override", "final", "explicit", "mutable", "register", "volatile", "noexcept",
	    "nullptr", "true", "false", "this"
	};
	return denylist;
}

} // anonymous namespace

bool keywords::is_keyword(std::string_view word) noexcept
{
	const auto &kws = get_keywords_set();
	return kws.find(word) != kws.end();
}

bool keywords::is_primitive_type(std::string_view word) noexcept
{
	const auto &prims = get_primitive_types_set();
	return prims.find(word) != prims.end();
}

bool keywords::is_function_specifier(std::string_view word) noexcept
{
	const auto &specifiers = get_function_specifiers_set();
	return specifiers.find(word) != specifiers.end();
}

bool keywords::is_std_type_or_container(std::string_view word) noexcept
{
	const auto &std_types = get_std_types_set();
	return std_types.find(word) != std_types.end();
}

bool keywords::is_call_candidate_denylisted(std::string_view identifier) noexcept
{
	if (identifier.length() < 2) {
		return true;
	}
	const auto &calls = get_call_denylist_set();
	return calls.find(identifier) != calls.end();
}

bool keywords::is_type_candidate_denylisted(std::string_view identifier) noexcept
{
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

	if (is_primitive_type(identifier)) {
		return true;
	}

	if (is_keyword(identifier)) {
		return true;
	}

	if (is_std_type_or_container(identifier)) {
		return true;
	}

	const auto &noise = get_noise_namespaces_set();
	return noise.find(identifier) != noise.end();
}

} // namespace languages::cpp
