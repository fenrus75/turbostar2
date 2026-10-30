#include "languages/cpp/symbol_extractor.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
#include "languages/cpp/keywords.h"
#include "languages/cpp/lexer.h"
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

// Finds matching closing brace at same brace_depth
size_t find_matching_brace(std::span<const token> tokens, size_t open_brace_idx)
{
	int target_depth = tokens[open_brace_idx].brace_depth;
	for (size_t i = open_brace_idx + 1; i < tokens.size(); ++i) {
		if (tokens[i].kind == token_kind::r_brace && tokens[i].brace_depth == target_depth) {
			return i;
		}
	}
	return tokens.size();
}

// Finds matching closing parenthesis at same paren_depth
size_t find_matching_paren(std::span<const token> tokens, size_t open_paren_idx)
{
	int target_depth = tokens[open_paren_idx].paren_depth;
	for (size_t i = open_paren_idx + 1; i < tokens.size(); ++i) {
		if (tokens[i].kind == token_kind::r_paren && tokens[i].paren_depth == target_depth) {
			return i;
		}
	}
	return tokens.size();
}

} // anonymous namespace

bool symbol_extractor::is_function_specifier_word(std::string_view word)
{
	return keywords::is_function_specifier(word);
}

void symbol_extractor::extract_symbols_from_source(std::string_view source, int min_lines, std::vector<tools::codemap_symbol_info> &out)
{
	auto tokens = lexer::tokenize(source);
	if (tokens.empty()) {
		return;
	}

	std::vector<namespace_scope> namespaces;
	std::vector<type_scope> types;

	// Pass 1: Extract namespaces and classes/structs to build scope hierarchy
	for (size_t i = 0; i < tokens.size(); ++i) {
		if ((i & 63) == 0 && project_manager::get_instance().is_exiting()) {
			return;
		}
		const auto &tok = tokens[i];

		// Check for namespace
		if (tok.kind == token_kind::keyword && tok.text == "namespace") {
			if (i > 0 && tokens[i - 1].kind == token_kind::keyword && tokens[i - 1].text == "using") {
				continue; // using namespace ...;
			}
			// Scan ahead for name or {
			std::string ns_name;
			bool is_alias = false;
			size_t open_brace_idx = tokens.size();

			for (size_t j = i + 1; j < std::min(tokens.size(), i + 40); ++j) {
				if (tokens[j].kind == token_kind::operator_sym && tokens[j].text == "=") {
					is_alias = true;
					break;
				}
				if (tokens[j].kind == token_kind::semicolon) {
					break;
				}
				if (tokens[j].kind == token_kind::l_brace) {
					open_brace_idx = j;
					break;
				}
				if (tokens[j].kind == token_kind::identifier || tokens[j].kind == token_kind::double_colon) {
					ns_name += tokens[j].text;
				}
			}

			if (!is_alias && open_brace_idx < tokens.size()) {
				size_t close_brace_idx = find_matching_brace(tokens, open_brace_idx);
				int end_line = (close_brace_idx < tokens.size()) ? tokens[close_brace_idx].line : tokens[open_brace_idx].line;
				std::string final_name = ns_name.empty() ? "(anonymous namespace)" : ns_name;
				namespaces.push_back({final_name, tok.line, end_line});
			}
			continue;
		}

		// Check for class or struct
		if (tok.kind == token_kind::keyword && (tok.text == "class" || tok.text == "struct")) {
			// Skip 'enum class' or 'enum struct'
			if (i > 0 && tokens[i - 1].kind == token_kind::keyword && tokens[i - 1].text == "enum") {
				continue;
			}
			// Skip parameter usages such as 'struct Foo *f, int x'
			if (i > 0 && (tokens[i - 1].kind == token_kind::l_paren || tokens[i - 1].kind == token_kind::comma)) {
				continue;
			}

			// Find class name
			std::string class_name;
			size_t name_idx = tokens.size();
			for (size_t j = i + 1; j < std::min(tokens.size(), i + 40); ++j) {
				if (tokens[j].kind == token_kind::semicolon || tokens[j].kind == token_kind::l_brace) {
					break;
				}
				if (tokens[j].text == "__attribute__" || tokens[j].text == "alignas") {
					for (size_t k = j + 1; k < std::min(tokens.size(), j + 5); ++k) {
						if (tokens[k].kind == token_kind::l_paren) {
							size_t close_paren = find_matching_paren(tokens, k);
							if (close_paren < tokens.size()) {
								j = close_paren;
							}
							break;
						}
					}
					continue;
				}
				if (tokens[j].kind == token_kind::l_bracket && j + 1 < tokens.size() && tokens[j + 1].kind == token_kind::l_bracket) {
					for (size_t k = j + 2; k < std::min(tokens.size(), j + 40); ++k) {
						if (tokens[k].kind == token_kind::r_bracket && k + 1 < tokens.size() && tokens[k + 1].kind == token_kind::r_bracket) {
							j = k + 1;
							break;
						}
					}
					continue;
				}
				if (tokens[j].kind == token_kind::identifier) {
					class_name = tokens[j].text;
					name_idx = j;
					break;
				}
			}

			if (!class_name.empty() && name_idx < tokens.size()) {
				// Scan forward to see if it's forward declaration (;) or definition ({)
				size_t open_brace_idx = tokens.size();
				bool is_forward_decl = false;
				for (size_t j = name_idx + 1; j < std::min(tokens.size(), name_idx + 60); ++j) {
					if (tokens[j].kind == token_kind::semicolon) {
						is_forward_decl = true;
						break;
					}
					if (tokens[j].kind == token_kind::l_brace && tokens[j].brace_depth == tok.brace_depth) {
						open_brace_idx = j;
						break;
					}
					// Reject if inside parameter list
					if (tok.paren_depth > 0 && (tokens[j].kind == token_kind::r_paren || tokens[j].kind == token_kind::comma)) {
						is_forward_decl = true;
						break;
					}
				}

				if (!is_forward_decl && open_brace_idx < tokens.size()) {
					size_t close_brace_idx = find_matching_brace(tokens, open_brace_idx);
					int end_line = (close_brace_idx < tokens.size()) ? tokens[close_brace_idx].line : tokens[open_brace_idx].line;
					std::string kind_val = (tok.text == "struct") ? "Struct" : "Class";
					types.push_back({class_name, kind_val, tok.line, end_line});
				}
			}
		}
	}

	// Emit namespace symbols
	for (const auto &ns : namespaces) {
		int len = ns.end_line - ns.start_line + 1;
		if (len >= min_lines) {
			auto qual = qualify_namespace(ns, namespaces);
			out.push_back({qual.qualified_name, qual.display_name, "Namespace", ns.start_line, ns.end_line, len, qual.depth, ""});
		}
	}

	// Emit class and struct symbols
	for (const auto &t : types) {
		int len = t.end_line - t.start_line + 1;
		if (len >= min_lines) {
			auto qual = qualify_symbol(t.start_line, t.name, namespaces, types, &t);
			out.push_back({qual.qualified_name, qual.display_name, t.kind, t.start_line, t.end_line, len, qual.depth, ""});
		}
	}

	// Pass 2: Extract enums and functions
	for (size_t i = 0; i < tokens.size(); ++i) {
		if ((i & 63) == 0 && project_manager::get_instance().is_exiting()) {
			return;
		}
		const auto &tok = tokens[i];

		// Check for enum
		if (tok.kind == token_kind::keyword && tok.text == "enum") {
			std::string enum_name;
			size_t name_idx = tokens.size();
			for (size_t j = i + 1; j < std::min(tokens.size(), i + 10); ++j) {
				if (tokens[j].kind == token_kind::keyword && (tokens[j].text == "class" || tokens[j].text == "struct")) {
					continue;
				}
				if (tokens[j].kind == token_kind::identifier) {
					enum_name = tokens[j].text;
					name_idx = j;
					break;
				}
				if (tokens[j].kind == token_kind::l_brace || tokens[j].kind == token_kind::semicolon) {
					break;
				}
			}

			if (!enum_name.empty() && name_idx < tokens.size()) {
				size_t open_brace_idx = tokens.size();
				bool is_proto = false;
				for (size_t j = name_idx + 1; j < std::min(tokens.size(), name_idx + 30); ++j) {
					if (tokens[j].kind == token_kind::semicolon) {
						is_proto = true;
						break;
					}
					if (tokens[j].kind == token_kind::l_brace) {
						open_brace_idx = j;
						break;
					}
				}

				if (!is_proto && open_brace_idx < tokens.size()) {
					size_t close_brace_idx = find_matching_brace(tokens, open_brace_idx);
					int end_line = (close_brace_idx < tokens.size()) ? tokens[close_brace_idx].line : tokens[open_brace_idx].line;
					int len = end_line - tok.line + 1;
					if (len >= min_lines) {
						auto qual = qualify_symbol(tok.line, enum_name, namespaces, types, nullptr);
						out.push_back({qual.qualified_name, qual.display_name, "Enum", tok.line, end_line, len, qual.depth, ""});
					}
				}
			}
			continue;
		}

		// Check for function definitions: identifier/operator followed by '('
		if (tok.kind == token_kind::l_paren && tok.paren_depth == 0 && i > 0) {
			size_t name_end_idx = i - 1;
			std::string func_name;
			size_t name_start_idx = name_end_idx;

			// Handle operator overloads
			if (tokens[name_end_idx].kind == token_kind::operator_sym) {
				if (name_end_idx > 0 && tokens[name_end_idx - 1].kind == token_kind::keyword && tokens[name_end_idx - 1].text == "operator") {
					func_name = "operator" + std::string(tokens[name_end_idx].text);
					name_start_idx = name_end_idx - 1;
				}
			} else if (tokens[name_end_idx].kind == token_kind::r_bracket && name_end_idx >= 2 &&
				   tokens[name_end_idx - 1].kind == token_kind::l_bracket &&
				   tokens[name_end_idx - 2].kind == token_kind::keyword && tokens[name_end_idx - 2].text == "operator") {
				func_name = "operator[]";
				name_start_idx = name_end_idx - 2;
			} else if (tokens[name_end_idx].kind == token_kind::r_paren && name_end_idx >= 2 &&
				   tokens[name_end_idx - 1].kind == token_kind::l_paren &&
				   tokens[name_end_idx - 2].kind == token_kind::keyword && tokens[name_end_idx - 2].text == "operator") {
				func_name = "operator()";
				name_start_idx = name_end_idx - 2;
			} else if (tokens[name_end_idx].kind == token_kind::identifier) {
				// Check for destructor '~ClassName'
				if (name_end_idx > 0 && tokens[name_end_idx - 1].kind == token_kind::tilde) {
					func_name = "~" + std::string(tokens[name_end_idx].text);
					name_start_idx = name_end_idx - 1;
				} else {
					func_name = std::string(tokens[name_end_idx].text);
					name_start_idx = name_end_idx;
				}
			}

			// Prepend any scope prefix: 'Scope::'
			while (name_start_idx >= 2 && tokens[name_start_idx - 1].kind == token_kind::double_colon &&
			       tokens[name_start_idx - 2].kind == token_kind::identifier) {
				func_name = std::string(tokens[name_start_idx - 2].text) + "::" + func_name;
				name_start_idx -= 2;
			}

			if (func_name.empty()) {
				continue;
			}

			// Filter out control flow keywords and non-function keywords
			std::string_view base_name = func_name;
			size_t last_col = base_name.rfind("::");
			if (last_col != std::string_view::npos) {
				base_name = base_name.substr(last_col + 2);
			}
			if (base_name == "if" || base_name == "for" || base_name == "while" || base_name == "switch" ||
			    base_name == "catch" || base_name == "__attribute__" || base_name == "sizeof" ||
			    base_name == "decltype" || base_name == "alignas" || base_name == "requires" ||
			    base_name.starts_with("__builtin_")) {
				continue;
			}

			// Match parameter list: find closing ')'
			size_t close_paren_idx = find_matching_paren(tokens, i);
			if (close_paren_idx >= tokens.size()) {
				continue;
			}

			// Scan forward from ')' to find '{' or ';'
			size_t open_brace_idx = tokens.size();
			bool is_prototype = false;
			for (size_t j = close_paren_idx + 1; j < std::min(tokens.size(), close_paren_idx + 80); ++j) {
				if (tokens[j].kind == token_kind::semicolon) {
					is_prototype = true;
					break;
				}
				if (tokens[j].kind == token_kind::l_brace) {
					open_brace_idx = j;
					break;
				}
			}

			if (is_prototype || open_brace_idx >= tokens.size()) {
				continue;
			}

			// Walk backward from name_start_idx to find true function start line (specifiers, return type, template)
			int func_start = tokens[name_start_idx].line;
			for (int k = static_cast<int>(name_start_idx) - 1; k >= 0; --k) {
				const auto &prev = tokens[k];
				if (prev.kind == token_kind::semicolon || prev.kind == token_kind::r_brace ||
				    prev.kind == token_kind::l_brace || prev.kind == token_kind::preprocessor) {
					break;
				}
				// Check for specifier or type keyword/identifier
				if (is_function_specifier_word(prev.text) || prev.kind == token_kind::identifier ||
				    prev.kind == token_kind::keyword || prev.kind == token_kind::operator_sym ||
				    prev.kind == token_kind::double_colon) {
					func_start = prev.line;
				} else {
					break;
				}
			}

			size_t close_brace_idx = find_matching_brace(tokens, open_brace_idx);
			int end_line = (close_brace_idx < tokens.size()) ? tokens[close_brace_idx].line : tokens[open_brace_idx].line;
			int len = std::max(1, end_line - func_start + 1);

			if (len >= min_lines) {
				auto qual = qualify_symbol(func_start, func_name, namespaces, types, nullptr);
				out.push_back({qual.qualified_name, qual.display_name, "Function", func_start, end_line, len, qual.depth, ""});
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

void symbol_extractor::extract_symbols(std::span<const std::string> lines, int min_lines, std::vector<tools::codemap_symbol_info> &out)
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
	extract_symbols_from_source(source, min_lines, out);
}

} // namespace languages::cpp
