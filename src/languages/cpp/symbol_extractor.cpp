#include "languages/cpp/symbol_extractor.h"

#include <algorithm>
#include <re2/re2.h>
#include <string>
#include <string_view>
#include <vector>
#include "languages/cpp/keywords.h"
#include "languages/cpp/source_masker.h"
#include "project_manager.h"
#include "utf8.h"

namespace languages::cpp
{

namespace
{

struct namespace_scope {
	std::string name;
	int start_line{0};
	int end_line{0};
};

struct type_scope {
	std::string name;
	std::string kind;
	int start_line{0};
	int end_line{0};
};

struct scope_qualification {
	std::string qualified_name;
	std::string display_name;
	int depth{0};
};

scope_qualification qualify_namespace(const namespace_scope &ns, std::span<const namespace_scope> all_namespaces)
{
	std::vector<const namespace_scope *> parents;
	for (const auto &other : all_namespaces) {
		if (&other != &ns && other.start_line <= ns.start_line && ns.end_line <= other.end_line) {
			parents.push_back(&other);
		}
	}
	std::sort(parents.begin(), parents.end(), [](const namespace_scope *a, const namespace_scope *b) {
		int span_a = a->end_line - a->start_line;
		int span_b = b->end_line - b->start_line;
		if (span_a != span_b) {
			return span_a > span_b;
		}
		return a->start_line < b->start_line;
	});

	std::string qualified = ns.name;
	if (!parents.empty()) {
		std::string prefix;
		for (const auto *p : parents) {
			if (!prefix.empty()) {
				prefix += "::";
			}
			prefix += p->name;
		}
		qualified = prefix + "::" + ns.name;
	}

	return {
	    .qualified_name = qualified,
	    .display_name = ns.name,
	    .depth = static_cast<int>(parents.size()),
	};
}

scope_qualification qualify_symbol(int line, std::string_view local_name, std::span<const namespace_scope> namespaces,
				  std::span<const type_scope> types, const type_scope *self_type)
{
	// 1. Find enclosing namespaces
	std::vector<const namespace_scope *> enc_ns;
	for (const auto &ns : namespaces) {
		if (ns.start_line <= line && line <= ns.end_line) {
			enc_ns.push_back(&ns);
		}
	}
	std::sort(enc_ns.begin(), enc_ns.end(), [](const namespace_scope *a, const namespace_scope *b) {
		int span_a = a->end_line - a->start_line;
		int span_b = b->end_line - b->start_line;
		if (span_a != span_b) {
			return span_a > span_b;
		}
		return a->start_line < b->start_line;
	});

	std::string ns_prefix;
	for (const auto *ns : enc_ns) {
		if (!ns_prefix.empty()) {
			ns_prefix += "::";
		}
		ns_prefix += ns->name;
	}

	// 2. Find enclosing types (classes / structs)
	std::vector<const type_scope *> enc_types;
	for (const auto &t : types) {
		if (&t != self_type && t.start_line <= line && line <= t.end_line) {
			enc_types.push_back(&t);
		}
	}
	std::sort(enc_types.begin(), enc_types.end(), [](const type_scope *a, const type_scope *b) {
		int span_a = a->end_line - a->start_line;
		int span_b = b->end_line - b->start_line;
		if (span_a != span_b) {
			return span_a > span_b;
		}
		return a->start_line < b->start_line;
	});

	std::string type_prefix;
	for (const auto *t : enc_types) {
		if (!type_prefix.empty()) {
			type_prefix += "::";
		}
		type_prefix += t->name;
	}

	// 3. Combine prefixes
	std::string full_prefix;
	if (!ns_prefix.empty() && !type_prefix.empty()) {
		full_prefix = ns_prefix + "::" + type_prefix;
	} else if (!ns_prefix.empty()) {
		full_prefix = ns_prefix;
	} else if (!type_prefix.empty()) {
		full_prefix = type_prefix;
	}

	// 4. Compute qualified name
	std::string qual_name;
	std::string name_str(local_name);
	if (full_prefix.empty()) {
		qual_name = name_str;
	} else if (name_str.starts_with(full_prefix + "::")) {
		qual_name = name_str;
	} else {
		if (!type_prefix.empty() && name_str.starts_with(type_prefix + "::") && full_prefix.ends_with(type_prefix)) {
			qual_name = full_prefix.substr(0, full_prefix.size() - type_prefix.size()) + name_str;
		} else if (!ns_prefix.empty() && name_str.starts_with(ns_prefix + "::")) {
			qual_name = name_str;
		} else {
			qual_name = full_prefix + "::" + name_str;
		}
	}

	// 5. Compute display name (short name)
	std::string disp_name;
	size_t last_colons = local_name.rfind("::");
	if (last_colons != std::string_view::npos) {
		disp_name = std::string(local_name.substr(last_colons + 2));
	} else {
		disp_name = name_str;
	}

	int depth = static_cast<int>(enc_ns.size() + enc_types.size());
	return {
	    .qualified_name = qual_name,
	    .display_name = disp_name,
	    .depth = depth,
	};
}

} // anonymous namespace

bool symbol_extractor::is_function_specifier_word(std::string_view word)
{
	return keywords::is_function_specifier(word);
}

void symbol_extractor::extract_symbols(std::span<const std::string> lines, int min_lines, std::vector<tools::codemap_symbol_info> &out)
{
	static const re2::RE2 cpp_namespace_regex(
	    R"(^\s*(?:inline\s+)?namespace(?:\s+\[\[.*?\]\])?(?:\s+([a-zA-Z_]\w*(?:::[a-zA-Z_]\w*)*))?(.*)$)");
	static const re2::RE2 cpp_class_regex(
	    R"(^(\s*(?:typedef\s+)?(?:class|struct)\s+(?:\[\[.*?\]\]\s+)?(?:__attribute__\s*\(\(.*?\)\)\s+)?([a-zA-Z_]\w*)))");
	static const re2::RE2 cpp_enum_regex(R"(^\s*(?:typedef\s+)?enum(?:\s+class|\s+struct)?\s+([a-zA-Z_]\w*))");
	static const re2::RE2 cpp_func_head_regex(
	    R"(^(\s*(?:[\w:<>]+[\s*&]*\s+)*[\s*&]*((?:[a-zA-Z_]\w*::)*(?:~[a-zA-Z_]\w*|operator\s*(?:[+\-*/%^&|~!=<>]+|\[\]|\(\)|->\*|->|,|new\[\]|delete\[\]|new|delete)|[a-zA-Z_]\w*))\s*\())");

	std::vector<std::string> masked_lines = source_masker::mask_lines(lines);

	std::vector<namespace_scope> namespaces;
	std::vector<type_scope> types;

	// Pass 1: Collect namespaces and classes/structs to establish scope hierarchies
	for (size_t i = 0; i < masked_lines.size(); ++i) {
		if ((i & 63) == 0 && project_manager::get_instance().is_exiting()) {
			return;
		}
		int line_num = static_cast<int>(i + 1);

		// Check for namespace
		std::string ns_name, rest;
		if (re2::RE2::FullMatch(masked_lines[i], cpp_namespace_regex, &ns_name, &rest)) {
			size_t eq_pos = rest.find('=');
			size_t semi_pos = rest.find(';');
			size_t brace_pos = rest.find('{');
			bool is_alias = (eq_pos != std::string::npos && (brace_pos == std::string::npos || eq_pos < brace_pos));
			bool is_proto = (semi_pos != std::string::npos && (brace_pos == std::string::npos || semi_pos < brace_pos));
			if (!is_alias && !is_proto) {
				bool found_open = (brace_pos != std::string::npos);
				size_t open_line = i;
				size_t open_col = found_open ? brace_pos : 0;
				if (!found_open) {
					for (size_t j = i + 1; j < std::min(masked_lines.size(), i + 15); ++j) {
						std::string_view l = masked_lines[j];
						size_t first = l.find_first_not_of(" \t\r\n");
						if (first == std::string_view::npos) {
							continue;
						}
						if (l[first] == ';' || l[first] == '=') {
							break;
						}
						size_t b = l.find('{');
						if (b != std::string_view::npos) {
							found_open = true;
							open_line = j;
							open_col = b;
							break;
						}
					}
				}
				if (found_open) {
					int depth = 0;
					bool started = false;
					size_t close_line = open_line;
					for (size_t j = open_line; j < masked_lines.size(); ++j) {
						size_t col_start = (j == open_line) ? open_col : 0;
						for (size_t c = col_start; c < masked_lines[j].size(); ++c) {
							if (masked_lines[j][c] == '{') {
								depth++;
								started = true;
							} else if (masked_lines[j][c] == '}') {
								depth--;
								if (started && depth == 0) {
									close_line = j;
									break;
								}
							}
						}
						if (started && depth == 0) {
							break;
						}
					}
					if (started && depth == 0) {
						std::string final_name = ns_name.empty() ? "(anonymous namespace)" : ns_name;
						namespaces.push_back({final_name, line_num, static_cast<int>(close_line + 1)});
					}
				}
			}
		}

		// Check for class / struct
		re2::StringPiece full_class_match, class_name_sp;
		if (re2::RE2::PartialMatch(masked_lines[i], cpp_class_regex, &full_class_match, &class_name_sp)) {
			bool is_class_candidate = true;
			std::string_view line_v = masked_lines[i];
			size_t match_end = static_cast<size_t>(full_class_match.data() - line_v.data()) + full_class_match.size();
			std::string_view suffix = (match_end <= line_v.size()) ? line_v.substr(match_end) : std::string_view{};
			size_t first_non_ws = suffix.find_first_not_of(" \t\r\n");
			if (first_non_ws != std::string_view::npos) {
				char next_ch = suffix[first_non_ws];
				if (next_ch == '*' || next_ch == '&' || next_ch == ',' || next_ch == ')') {
					is_class_candidate = false;
				}
			}
			size_t close_paren = suffix.find(')');
			size_t open_brace = suffix.find('{');
			if (close_paren != std::string_view::npos && (open_brace == std::string_view::npos || close_paren < open_brace)) {
				is_class_candidate = false;
			}
			if (is_class_candidate) {
				std::string_view trimmed_line = masked_lines[i];
				size_t last_non_ws = trimmed_line.find_last_not_of(" \t\r\n");
				if (last_non_ws == std::string_view::npos || trimmed_line[last_non_ws] != ';') {
					int end_line = line_num;
					int depth = 0;
					bool started = false;
					for (size_t j = i; j < masked_lines.size(); ++j) {
						for (char c : masked_lines[j]) {
							if (c == '{') {
								depth++;
								started = true;
							} else if (c == '}') {
								depth--;
							}
						}
						if (started && depth == 0) {
							end_line = static_cast<int>(j + 1);
							break;
						}
					}
					if (started) {
						std::string kind_val = (masked_lines[i].find("struct") != std::string::npos) ? "Struct" : "Class";
						types.push_back({std::string(class_name_sp), kind_val, line_num, end_line});
					}
				}
			}
		}
	}

	// Emit namespaces
	for (const auto &ns : namespaces) {
		int len = ns.end_line - ns.start_line + 1;
		if (len >= min_lines) {
			auto qual = qualify_namespace(ns, namespaces);
			out.push_back({qual.qualified_name, qual.display_name, "Namespace", ns.start_line, ns.end_line, len, qual.depth, ""});
		}
	}

	// Emit classes and structs
	for (const auto &t : types) {
		int len = t.end_line - t.start_line + 1;
		if (len >= min_lines) {
			auto qual = qualify_symbol(t.start_line, t.name, namespaces, types, &t);
			out.push_back({qual.qualified_name, qual.display_name, t.kind, t.start_line, t.end_line, len, qual.depth, ""});
		}
	}

	// Pass 2: Collect enums and functions
	for (size_t i = 0; i < masked_lines.size(); ++i) {
		if ((i & 63) == 0 && project_manager::get_instance().is_exiting()) {
			return;
		}
		int line_num = static_cast<int>(i + 1);

		std::string enum_name;
		if (re2::RE2::PartialMatch(masked_lines[i], cpp_enum_regex, &enum_name)) {
			std::string_view trimmed_line = masked_lines[i];
			size_t last_non_ws = trimmed_line.find_last_not_of(" \t\r\n");
			if (last_non_ws != std::string_view::npos && trimmed_line[last_non_ws] == ';') {
				continue;
			}

			int end_line = line_num;
			int depth = 0;
			bool started = false;
			for (size_t j = i; j < masked_lines.size(); ++j) {
				for (char c : masked_lines[j]) {
					if (c == '{') {
						depth++;
						started = true;
					} else if (c == '}') {
						depth--;
					}
				}
				if (started && depth == 0) {
					end_line = static_cast<int>(j + 1);
					break;
				}
			}
			if (started) {
				int len = end_line - line_num + 1;
				if (len >= min_lines) {
					auto qual = qualify_symbol(line_num, enum_name, namespaces, types, nullptr);
					out.push_back({qual.qualified_name, qual.display_name, "Enum", line_num, end_line, len, qual.depth, ""});
				}
			}
			continue;
		}

		re2::StringPiece full_func_match, func_name_sp;
		if (re2::RE2::PartialMatch(masked_lines[i], cpp_func_head_regex, &full_func_match, &func_name_sp)) {
			std::string name(func_name_sp);
			if (name != "if" && name != "for" && name != "while" && name != "switch" && name != "catch" &&
			    name != "__attribute__" && !name.starts_with("__builtin_")) {
				// Forward scan to find body '{' and verify it's not a prototype ';'
				int paren_depth = 0;
				bool found_brace = false;
				size_t brace_line_idx = i;
				bool is_prototype = false;
				size_t func_head_pos = static_cast<size_t>(full_func_match.data() - masked_lines[i].data());

				for (size_t j = i; j < std::min(masked_lines.size(), i + 40); ++j) {
					std::string_view scan_line = masked_lines[j];
					for (size_t char_idx = (j == i ? func_head_pos : 0); char_idx < scan_line.size(); ++char_idx) {
						char c = scan_line[char_idx];
						if (c == '(') {
							paren_depth++;
						} else if (c == ')') {
							paren_depth--;
						} else if (c == ';' && paren_depth <= 0) {
							is_prototype = true;
							break;
						} else if (c == '{' && paren_depth <= 0) {
							found_brace = true;
							brace_line_idx = j;
							break;
						}
					}
					if (is_prototype || found_brace) {
						break;
					}
				}

				if (!found_brace || is_prototype) {
					continue;
				}

				// Find upper bound by searching backwards from line i for previous '}'
				int upper_bound_idx = -1;
				for (int k = static_cast<int>(i) - 1; k >= 0; --k) {
					if (masked_lines[k].find('}') != std::string::npos) {
						upper_bound_idx = k;
						break;
					}
				}

				// Walk backwards from line i to find the true function start line
				int func_start = static_cast<int>(i + 1);
				for (int k = static_cast<int>(i) - 1; k > upper_bound_idx; --k) {
					std::string_view l_view = masked_lines[k];
					size_t first_non = l_view.find_first_not_of(" \t\r\n");
					if (first_non == std::string_view::npos) {
						// Blank line provides a bound on the start of the function
						break;
					}
					std::string_view trimmed = l_view.substr(first_non);
					if (trimmed.starts_with('#') || trimmed.starts_with("/*") || trimmed.ends_with("*/") ||
					    trimmed.find(';') != std::string_view::npos || trimmed.find('}') != std::string_view::npos) {
						break;
					}

					// Check if line contains function specifier keywords (static, void, inline, etc.)
					bool has_specifier = false;
					std::string token;
					for (size_t ci = 0; ci <= trimmed.size(); ++ci) {
						char c = (ci < trimmed.size()) ? trimmed[ci] : ' ';
						if (utf8::is_alnum(c, "_")) {
							token += c;
						} else {
							if (!token.empty()) {
								if (is_function_specifier_word(token)) {
									has_specifier = true;
									break;
								}
								token.clear();
							}
						}
					}
					if (has_specifier) {
						func_start = k + 1;
					} else {
						break;
					}
				}

				// Match braces starting from brace_line_idx to determine end_line
				int end_line = func_start;
				int depth = 0;
				bool started = false;
				for (size_t j = brace_line_idx; j < masked_lines.size(); ++j) {
					for (char c : masked_lines[j]) {
						if (c == '{') {
							depth++;
							started = true;
						} else if (c == '}') {
							depth--;
						}
					}
					if (started && depth == 0) {
						end_line = static_cast<int>(j + 1);
						break;
					}
				}

				if (started) {
					int len = std::max(1, end_line - func_start + 1);
					if (len >= min_lines) {
						auto qual = qualify_symbol(func_start, name, namespaces, types, nullptr);
						out.push_back({qual.qualified_name, qual.display_name, "Function", func_start, end_line, len, qual.depth, ""});
					}
				}
			}
		}
	}

	// Sort symbols by start line, larger span first on ties
	std::sort(out.begin(), out.end(), [](const tools::codemap_symbol_info &a, const tools::codemap_symbol_info &b) {
		if (a.start_line != b.start_line) {
			return a.start_line < b.start_line;
		}
		if (a.end_line != b.end_line) {
			return a.end_line > b.end_line;
		}
		return a.name < b.name;
	});
}

} // namespace languages::cpp
