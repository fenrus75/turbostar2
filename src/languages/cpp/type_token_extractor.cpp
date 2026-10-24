#include "languages/cpp/type_token_extractor.h"
#include <algorithm>
#include <unordered_set>
#include "languages/cpp/keywords.h"
#include "languages/cpp/lexer.h"

namespace languages::cpp
{

std::vector<source_token> type_token_extractor::extract_candidates_from_source(std::string_view source, int start_line)
{
	std::vector<source_token> result;
	std::unordered_set<std::string> seen;

	auto tokens = lexer::tokenize(source);
	auto check_and_add = [&](std::string_view candidate, int line, int col) {
		std::string cand_str(candidate);
		if (!keywords::is_type_candidate_denylisted(cand_str) && seen.find(cand_str) == seen.end()) {
			seen.insert(cand_str);
			int line_num = start_line + line - 1;
			int col_num = std::max(0, col - 1);
			result.push_back({std::move(cand_str), line_num, col_num});
			return result.size() >= 15;
		}
		return false;
	};

	for (size_t i = 0; i < tokens.size(); ++i) {
		const auto &tok = tokens[i];

		// Pattern 1: (class|struct|enum) Identifier
		if (tok.kind == token_kind::keyword && (tok.text == "class" || tok.text == "struct" || tok.text == "enum")) {
			if (i + 1 < tokens.size() && tokens[i + 1].kind == token_kind::identifier) {
				if (check_and_add(tokens[i + 1].text, tokens[i + 1].line, tokens[i + 1].col)) {
					return result;
				}
			}
			continue;
		}

		// Pattern 2: Identifier::
		if (tok.kind == token_kind::identifier && i + 1 < tokens.size() && tokens[i + 1].kind == token_kind::double_colon) {
			if (check_and_add(tok.text, tok.line, tok.col)) {
				return result;
			}
			continue;
		}

		// Pattern 3: < [const] Identifier [*|&] >
		if (tok.kind == token_kind::operator_sym && tok.text == "<") {
			size_t j = i + 1;
			if (j < tokens.size() && tokens[j].kind == token_kind::keyword && tokens[j].text == "const") {
				++j;
			}
			if (j < tokens.size() && tokens[j].kind == token_kind::identifier) {
				size_t id_idx = j;
				++j;
				if (j < tokens.size() && tokens[j].kind == token_kind::operator_sym &&
				    (tokens[j].text == "*" || tokens[j].text == "&")) {
					++j;
				}
				if (j < tokens.size() && tokens[j].kind == token_kind::operator_sym && tokens[j].text == ">") {
					if (check_and_add(tokens[id_idx].text, tokens[id_idx].line, tokens[id_idx].col)) {
						return result;
					}
				}
			}
			continue;
		}

		// Pattern 4: Identifier [const] [*|&]+ Identifier
		if (tok.kind == token_kind::identifier && i + 2 < tokens.size()) {
			size_t j = i + 1;
			if (j < tokens.size() && tokens[j].kind == token_kind::keyword && tokens[j].text == "const") {
				++j;
			}
			bool has_ptr_or_ref = false;
			while (j < tokens.size() && tokens[j].kind == token_kind::operator_sym &&
			       (tokens[j].text == "*" || tokens[j].text == "&")) {
				has_ptr_or_ref = true;
				++j;
			}
			if (has_ptr_or_ref && j < tokens.size() && tokens[j].kind == token_kind::identifier) {
				if (check_and_add(tok.text, tok.line, tok.col)) {
					return result;
				}
				continue;
			}
		}

		// Pattern 5: [const] Identifier [*|&]* Identifier [();,{}]
		size_t type_idx = i;
		if (tok.kind == token_kind::keyword && tok.text == "const") {
			type_idx = i + 1;
		}
		if (type_idx < tokens.size() && tokens[type_idx].kind == token_kind::identifier) {
			size_t j = type_idx + 1;
			while (j < tokens.size() && tokens[j].kind == token_kind::operator_sym &&
			       (tokens[j].text == "*" || tokens[j].text == "&")) {
				++j;
			}
			if (j < tokens.size() && tokens[j].kind == token_kind::identifier) {
				size_t var_idx = j;
				size_t next_idx = var_idx + 1;
				if (next_idx < tokens.size()) {
					const auto &next = tokens[next_idx];
					if (next.kind == token_kind::semicolon || next.kind == token_kind::comma ||
					    next.kind == token_kind::l_paren || next.kind == token_kind::r_paren ||
					    next.kind == token_kind::l_brace || next.kind == token_kind::r_brace ||
					    (next.kind == token_kind::operator_sym && next.text == "=")) {
						if (check_and_add(tokens[type_idx].text, tokens[type_idx].line, tokens[type_idx].col)) {
							return result;
						}
					}
				}
			}
		}
	}

	return result;
}

std::vector<source_token> type_token_extractor::extract_candidates(std::span<const std::string> lines, int start_line)
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
