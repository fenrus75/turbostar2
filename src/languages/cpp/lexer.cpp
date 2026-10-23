#include "languages/cpp/lexer.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>
#include "languages/cpp/keywords.h"
#include "utf8.h"

namespace languages::cpp
{

std::string_view lexer::token_kind_to_string(token_kind kind) noexcept
{
	switch (kind) {
	case token_kind::eof:
		return "eof";
	case token_kind::identifier:
		return "identifier";
	case token_kind::keyword:
		return "keyword";
	case token_kind::number:
		return "number";
	case token_kind::string_literal:
		return "string_literal";
	case token_kind::character_literal:
		return "character_literal";
	case token_kind::comment:
		return "comment";
	case token_kind::preprocessor:
		return "preprocessor";
	case token_kind::l_paren:
		return "(";
	case token_kind::r_paren:
		return ")";
	case token_kind::l_brace:
		return "{";
	case token_kind::r_brace:
		return "}";
	case token_kind::l_bracket:
		return "[";
	case token_kind::r_bracket:
		return "]";
	case token_kind::semicolon:
		return ";";
	case token_kind::colon:
		return ":";
	case token_kind::double_colon:
		return "::";
	case token_kind::comma:
		return ",";
	case token_kind::dot:
		return ".";
	case token_kind::arrow:
		return "->";
	case token_kind::tilde:
		return "~";
	case token_kind::operator_sym:
		return "operator";
	case token_kind::other:
		return "other";
	}
	return "unknown";
}

std::vector<token> lexer::tokenize(std::string_view source)
{
	std::vector<token> tokens;
	tokens.reserve(source.size() / 8);

	size_t pos = 0;
	int line = 1;
	int col = 1;
	int brace_depth = 0;
	int paren_depth = 0;
	int bracket_depth = 0;
	bool at_line_start = true;

	while (pos < source.size()) {
		char c = source[pos];

		// 1. Whitespace handling
		if (c == ' ' || c == '\t' || c == '\r') {
			++pos;
			++col;
			continue;
		}
		if (c == '\n') {
			++pos;
			++line;
			col = 1;
			at_line_start = true;
			continue;
		}

		// 2. Preprocessor directive (# at start of line)
		if (at_line_start && c == '#') {
			size_t start_pos = pos;
			int start_col = col;
			int start_line = line;
			while (pos < source.size()) {
				if (source[pos] == '\\' && pos + 1 < source.size() && source[pos + 1] == '\n') {
					pos += 2;
					++line;
					col = 1;
					continue;
				}
				if (source[pos] == '\n') {
					break;
				}
				++pos;
				++col;
			}
			tokens.push_back({token_kind::preprocessor, source.substr(start_pos, pos - start_pos),
					  start_line, start_col, brace_depth, paren_depth, bracket_depth});
			at_line_start = false;
			continue;
		}

		at_line_start = false;

		// 3. Comments
		if (c == '/' && pos + 1 < source.size()) {
			char next_c = source[pos + 1];
			if (next_c == '/') {
				size_t start_pos = pos;
				int start_col = col;
				int start_line = line;
				pos += 2;
				col += 2;
				while (pos < source.size() && source[pos] != '\n') {
					++pos;
					++col;
				}
				tokens.push_back({token_kind::comment, source.substr(start_pos, pos - start_pos),
						  start_line, start_col, brace_depth, paren_depth, bracket_depth});
				continue;
			}
			if (next_c == '*') {
				size_t start_pos = pos;
				int start_col = col;
				int start_line = line;
				pos += 2;
				col += 2;
				bool closed = false;
				while (pos + 1 < source.size()) {
					if (source[pos] == '*' && source[pos + 1] == '/') {
						pos += 2;
						col += 2;
						closed = true;
						break;
					}
					if (source[pos] == '\n') {
						++pos;
						++line;
						col = 1;
					} else {
						++pos;
						++col;
					}
				}
				if (!closed) {
					pos = source.size();
				}
				tokens.push_back({token_kind::comment, source.substr(start_pos, pos - start_pos),
						  start_line, start_col, brace_depth, paren_depth, bracket_depth});
				continue;
			}
		}

		// 4. Raw string literal: R"delim(...)delim"
		if (c == 'R' && pos + 1 < source.size() && source[pos + 1] == '"') {
			size_t start_pos = pos;
			int start_col = col;
			int start_line = line;
			size_t p = pos + 2;
			size_t paren = source.find('(', p);
			if (paren != std::string_view::npos && (paren - p) <= 16) {
				std::string_view delim = source.substr(p, paren - p);
				std::string closing = ")" + std::string(delim) + "\"";
				size_t end = source.find(closing, paren + 1);
				if (end != std::string_view::npos) {
					size_t match_end = end + closing.size();
					for (size_t k = pos; k < match_end; ++k) {
						if (source[k] == '\n') {
							++line;
							col = 1;
						} else {
							++col;
						}
					}
					size_t total_len = match_end - start_pos;
					pos = match_end;
					tokens.push_back({token_kind::string_literal, source.substr(start_pos, total_len),
							  start_line, start_col, brace_depth, paren_depth, bracket_depth});
					continue;
				}
			}
		}

		// 5. Regular string literal: " ... "
		if (c == '"') {
			size_t start_pos = pos;
			int start_col = col;
			int start_line = line;
			++pos;
			++col;
			bool escaped = false;
			while (pos < source.size()) {
				char sc = source[pos];
				if (escaped) {
					escaped = false;
					if (sc == '\n') {
						++line;
						col = 1;
					} else {
						++col;
					}
					++pos;
					continue;
				}
				if (sc == '\\') {
					escaped = true;
					++pos;
					++col;
					continue;
				}
				if (sc == '"') {
					++pos;
					++col;
					break;
				}
				if (sc == '\n') {
					// Unclosed string literal: recover at newline
					break;
				}
				++pos;
				++col;
			}
			tokens.push_back({token_kind::string_literal, source.substr(start_pos, pos - start_pos),
					  start_line, start_col, brace_depth, paren_depth, bracket_depth});
			continue;
		}

		// 6. Character literal: ' ... '
		if (c == '\'') {
			size_t start_pos = pos;
			int start_col = col;
			int start_line = line;
			++pos;
			++col;
			bool escaped = false;
			while (pos < source.size()) {
				char sc = source[pos];
				if (escaped) {
					escaped = false;
					if (sc == '\n') {
						++line;
						col = 1;
					} else {
						++col;
					}
					++pos;
					continue;
				}
				if (sc == '\\') {
					escaped = true;
					++pos;
					++col;
					continue;
				}
				if (sc == '\'') {
					++pos;
					++col;
					break;
				}
				if (sc == '\n') {
					// Unclosed character literal: recover at newline
					break;
				}
				++pos;
				++col;
			}
			tokens.push_back({token_kind::character_literal, source.substr(start_pos, pos - start_pos),
					  start_line, start_col, brace_depth, paren_depth, bracket_depth});
			continue;
		}

		// 7. Numbers
		if (std::isdigit(static_cast<unsigned char>(c)) ||
		    (c == '.' && pos + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[pos + 1])))) {
			size_t start_pos = pos;
			int start_col = col;
			int start_line = line;
			bool is_hex = false;
			if (c == '0' && pos + 1 < source.size() && (source[pos + 1] == 'x' || source[pos + 1] == 'X')) {
				is_hex = true;
				pos += 2;
				col += 2;
			}
			while (pos < source.size()) {
				char nc = source[pos];
				if (is_hex) {
					if (std::isxdigit(static_cast<unsigned char>(nc)) || nc == '.' || nc == '\'' ||
					    nc == 'p' || nc == 'P') {
						++pos;
						++col;
					} else {
						break;
					}
				} else {
					if (std::isdigit(static_cast<unsigned char>(nc)) || nc == '.' || nc == '\'' ||
					    nc == 'e' || nc == 'E' || nc == 'b' || nc == 'B') {
						++pos;
						++col;
					} else {
						break;
					}
				}
			}
			// Consume type suffixes (u, l, f, z, etc.)
			while (pos < source.size() && (std::isalpha(static_cast<unsigned char>(source[pos])) || source[pos] == '_')) {
				++pos;
				++col;
			}
			tokens.push_back({token_kind::number, source.substr(start_pos, pos - start_pos),
					  start_line, start_col, brace_depth, paren_depth, bracket_depth});
			continue;
		}

		// 8. Identifiers and Keywords
		if (utf8::is_alnum(c, "_") && !std::isdigit(static_cast<unsigned char>(c))) {
			size_t start_pos = pos;
			int start_col = col;
			int start_line = line;
			while (pos < source.size() && utf8::is_alnum(source[pos], "_")) {
				++pos;
				++col;
			}
			std::string_view word = source.substr(start_pos, pos - start_pos);
			token_kind k = keywords::is_keyword(word) ? token_kind::keyword : token_kind::identifier;
			tokens.push_back({k, word, start_line, start_col, brace_depth, paren_depth, bracket_depth});
			continue;
		}

		// 9. Punctuation with robust depth tracking
		if (c == '{') {
			tokens.push_back({token_kind::l_brace, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
			++brace_depth;
			++pos;
			++col;
			continue;
		}
		if (c == '}') {
			// Clamping prevents underflow on stray or unbalanced closing braces
			brace_depth = std::max(0, brace_depth - 1);
			tokens.push_back({token_kind::r_brace, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
			++pos;
			++col;
			continue;
		}
		if (c == '(') {
			tokens.push_back({token_kind::l_paren, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
			++paren_depth;
			++pos;
			++col;
			continue;
		}
		if (c == ')') {
			paren_depth = std::max(0, paren_depth - 1);
			tokens.push_back({token_kind::r_paren, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
			++pos;
			++col;
			continue;
		}
		if (c == '[') {
			tokens.push_back({token_kind::l_bracket, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
			++bracket_depth;
			++pos;
			++col;
			continue;
		}
		if (c == ']') {
			bracket_depth = std::max(0, bracket_depth - 1);
			tokens.push_back({token_kind::r_bracket, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
			++pos;
			++col;
			continue;
		}
		if (c == ';') {
			tokens.push_back({token_kind::semicolon, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
			++pos;
			++col;
			continue;
		}
		if (c == ':' && pos + 1 < source.size() && source[pos + 1] == ':') {
			tokens.push_back({token_kind::double_colon, source.substr(pos, 2), line, col, brace_depth, paren_depth, bracket_depth});
			pos += 2;
			col += 2;
			continue;
		}
		if (c == ':') {
			tokens.push_back({token_kind::colon, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
			++pos;
			++col;
			continue;
		}
		if (c == ',') {
			tokens.push_back({token_kind::comma, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
			++pos;
			++col;
			continue;
		}
		if (c == '-' && pos + 1 < source.size() && source[pos + 1] == '>') {
			size_t len = (pos + 2 < source.size() && source[pos + 2] == '*') ? 3 : 2;
			tokens.push_back({token_kind::arrow, source.substr(pos, len), line, col, brace_depth, paren_depth, bracket_depth});
			pos += len;
			col += static_cast<int>(len);
			continue;
		}
		if (c == '.') {
			tokens.push_back({token_kind::dot, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
			++pos;
			++col;
			continue;
		}
		if (c == '~') {
			tokens.push_back({token_kind::tilde, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
			++pos;
			++col;
			continue;
		}

		// 10. Multi-character and compound operators
		static const std::string_view two_char_ops[] = {
		    "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "++", "--", "<=>"
		};
		bool matched_op = false;
		if (pos + 2 < source.size() && source.substr(pos, 3) == "<=>") {
			tokens.push_back({token_kind::operator_sym, source.substr(pos, 3), line, col, brace_depth, paren_depth, bracket_depth});
			pos += 3;
			col += 3;
			matched_op = true;
		} else if (pos + 1 < source.size()) {
			std::string_view candidate = source.substr(pos, 2);
			for (const auto &op : two_char_ops) {
				if (candidate == op) {
					tokens.push_back({token_kind::operator_sym, candidate, line, col, brace_depth, paren_depth, bracket_depth});
					pos += 2;
					col += 2;
					matched_op = true;
					break;
				}
			}
		}
		if (matched_op) {
			continue;
		}

		// Single character operator or punctuation
		if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '&' || c == '|' ||
		    c == '^' || c == '!' || c == '=' || c == '<' || c == '>' || c == '?') {
			tokens.push_back({token_kind::operator_sym, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
			++pos;
			++col;
			continue;
		}

		// 11. Other unrecognized characters
		tokens.push_back({token_kind::other, source.substr(pos, 1), line, col, brace_depth, paren_depth, bracket_depth});
		++pos;
		++col;
	}

	// 12. Sentinel EOF token
	tokens.push_back({token_kind::eof, "", line, col, brace_depth, paren_depth, bracket_depth});
	return tokens;
}

} // namespace languages::cpp
