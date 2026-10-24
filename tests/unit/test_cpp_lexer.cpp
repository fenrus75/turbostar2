// Tested source file: src/languages/cpp/lexer.cpp

#include "languages/cpp/lexer.h"
#include <cassert>
#include <iostream>
#include <string_view>
#include <vector>
#include "unit/test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog(30);

	std::cout << "Testing languages::cpp::lexer..." << std::endl;

	// 1. Basic tokens: keywords, identifiers, numbers, punctuation
	{
		std::string_view src = "int main() {\n    return 42;\n}";
		auto tokens = languages::cpp::lexer::tokenize(src);

		assert(!tokens.empty());
		// int (keyword)
		assert(tokens[0].kind == languages::cpp::token_kind::keyword);
		assert(tokens[0].text == "int");
		assert(tokens[0].line == 1);
		assert(tokens[0].col == 1);
		assert(tokens[0].brace_depth == 0);

		// main (identifier)
		assert(tokens[1].kind == languages::cpp::token_kind::identifier);
		assert(tokens[1].text == "main");
		assert(tokens[1].line == 1);
		assert(tokens[1].col == 5);

		// (
		assert(tokens[2].kind == languages::cpp::token_kind::l_paren);
		assert(tokens[2].paren_depth == 0);

		// )
		assert(tokens[3].kind == languages::cpp::token_kind::r_paren);
		assert(tokens[3].paren_depth == 0);

		// {
		assert(tokens[4].kind == languages::cpp::token_kind::l_brace);
		assert(tokens[4].brace_depth == 0); // At the { itself, before entering body

		// return (keyword)
		assert(tokens[5].kind == languages::cpp::token_kind::keyword);
		assert(tokens[5].text == "return");
		assert(tokens[5].line == 2);
		assert(tokens[5].brace_depth == 1);

		// 42 (number)
		assert(tokens[6].kind == languages::cpp::token_kind::number);
		assert(tokens[6].text == "42");
		assert(tokens[6].brace_depth == 1);

		// ;
		assert(tokens[7].kind == languages::cpp::token_kind::semicolon);
		assert(tokens[7].brace_depth == 1);

		// }
		assert(tokens[8].kind == languages::cpp::token_kind::r_brace);
		assert(tokens[8].line == 3);
		assert(tokens[8].brace_depth == 0);

		// EOF sentinel
		assert(tokens[9].kind == languages::cpp::token_kind::eof);
	}

	// 2. Stray closing brace resilience (user requirement)
	// When brace_depth is 0 and an extra } appears, depth must stay 0 and NOT underflow.
	{
		std::string_view src = "void f() {}\n}\nvoid g() {}";
		auto tokens = languages::cpp::lexer::tokenize(src);

		// Find the stray '}'
		size_t stray_idx = 0;
		for (size_t i = 0; i < tokens.size(); ++i) {
			if (tokens[i].kind == languages::cpp::token_kind::r_brace && tokens[i].line == 2) {
				stray_idx = i;
				break;
			}
		}
		assert(stray_idx > 0);
		assert(tokens[stray_idx].brace_depth == 0);

		// Verify that 'g' on line 3 is still recognized at brace_depth 0!
		size_t g_idx = 0;
		for (size_t i = stray_idx; i < tokens.size(); ++i) {
			if (tokens[i].kind == languages::cpp::token_kind::identifier && tokens[i].text == "g") {
				g_idx = i;
				break;
			}
		}
		assert(g_idx > 0);
		assert(tokens[g_idx].brace_depth == 0);
		assert(tokens[g_idx].line == 3);
	}

	// 3. String literals (regular, escaped, raw strings) and characters
	{
		std::string_view src = R"(
			const char *s = "hello \"world\"\n";
			char c = 'x';
			char esc = '\'';
			const char *raw = R"custom(nested "quotes" and (parens) )custom";
		)";
		auto tokens = languages::cpp::lexer::tokenize(src);

		bool found_str = false;
		bool found_char = false;
		bool found_raw = false;
		for (const auto &tok : tokens) {
			if (tok.kind == languages::cpp::token_kind::string_literal) {
				if (tok.text.starts_with("\"hello")) {
					found_str = true;
				} else if (tok.text.starts_with("R\"custom")) {
					found_raw = true;
					assert(tok.text.ends_with(")custom\""));
				}
			} else if (tok.kind == languages::cpp::token_kind::character_literal) {
				if (tok.text == "'x'") {
					found_char = true;
				}
			}
		}
		assert(found_str);
		assert(found_char);
		assert(found_raw);
	}

	// 4. Comments (single-line, multi-line, unclosed multi-line)
	{
		std::string_view src = "// line comment\n/* block comment */\nint a = 1;\n/* unclosed at eof";
		auto tokens = languages::cpp::lexer::tokenize(src);

		assert(tokens[0].kind == languages::cpp::token_kind::comment);
		assert(tokens[0].text == "// line comment");
		assert(tokens[1].kind == languages::cpp::token_kind::comment);
		assert(tokens[1].text == "/* block comment */");

		bool found_unclosed = false;
		for (const auto &tok : tokens) {
			if (tok.kind == languages::cpp::token_kind::comment && tok.text.starts_with("/* unclosed")) {
				found_unclosed = true;
			}
		}
		assert(found_unclosed);
		assert(tokens.back().kind == languages::cpp::token_kind::eof);
	}

	// 5. Preprocessor directives
	{
		std::string_view src = "#include <vector>\n#define MAX(a, b) ((a) > (b) ? (a) : (b))\nint x = 0;";
		auto tokens = languages::cpp::lexer::tokenize(src);

		assert(tokens[0].kind == languages::cpp::token_kind::preprocessor);
		assert(tokens[0].text == "#include <vector>");
		assert(tokens[1].kind == languages::cpp::token_kind::preprocessor);
		assert(tokens[1].text.starts_with("#define MAX"));

		// 'int' must not be inside preprocessor
		assert(tokens[2].kind == languages::cpp::token_kind::keyword);
		assert(tokens[2].text == "int");
		assert(tokens[2].line == 3);
	}

	// 6. Operators and double colon
	{
		std::string_view src = "std::vector<int>::iterator it = v.begin() + 1 -> field;";
		auto tokens = languages::cpp::lexer::tokenize(src);

		bool found_double_colon = false;
		bool found_arrow = false;
		for (const auto &tok : tokens) {
			if (tok.kind == languages::cpp::token_kind::double_colon) {
				found_double_colon = true;
			} else if (tok.kind == languages::cpp::token_kind::arrow) {
				found_arrow = true;
			}
		}
		assert(found_double_colon);
		assert(found_arrow);
	}

	// 7. Unclosed string literal resilience (recovers at newline)
	{
		std::string_view src = "const char *broken = \"unclosed string;\nint next_var = 100;";
		auto tokens = languages::cpp::lexer::tokenize(src);

		bool found_next = false;
		for (const auto &tok : tokens) {
			if (tok.kind == languages::cpp::token_kind::identifier && tok.text == "next_var") {
				found_next = true;
				assert(tok.line == 2);
			}
		}
		assert(found_next);
	}

	// 8. token_kind_to_string coverage
	{
		using tk = languages::cpp::token_kind;
		assert(languages::cpp::lexer::token_kind_to_string(tk::eof) == "eof");
		assert(languages::cpp::lexer::token_kind_to_string(tk::identifier) == "identifier");
		assert(languages::cpp::lexer::token_kind_to_string(tk::keyword) == "keyword");
		assert(languages::cpp::lexer::token_kind_to_string(tk::number) == "number");
		assert(languages::cpp::lexer::token_kind_to_string(tk::string_literal) == "string_literal");
		assert(languages::cpp::lexer::token_kind_to_string(tk::character_literal) == "character_literal");
		assert(languages::cpp::lexer::token_kind_to_string(tk::comment) == "comment");
		assert(languages::cpp::lexer::token_kind_to_string(tk::preprocessor) == "preprocessor");
		assert(languages::cpp::lexer::token_kind_to_string(tk::l_paren) == "(");
		assert(languages::cpp::lexer::token_kind_to_string(tk::r_paren) == ")");
		assert(languages::cpp::lexer::token_kind_to_string(tk::l_brace) == "{");
		assert(languages::cpp::lexer::token_kind_to_string(tk::r_brace) == "}");
		assert(languages::cpp::lexer::token_kind_to_string(tk::l_bracket) == "[");
		assert(languages::cpp::lexer::token_kind_to_string(tk::r_bracket) == "]");
		assert(languages::cpp::lexer::token_kind_to_string(tk::semicolon) == ";");
		assert(languages::cpp::lexer::token_kind_to_string(tk::colon) == ":");
		assert(languages::cpp::lexer::token_kind_to_string(tk::double_colon) == "::");
		assert(languages::cpp::lexer::token_kind_to_string(tk::comma) == ",");
		assert(languages::cpp::lexer::token_kind_to_string(tk::dot) == ".");
		assert(languages::cpp::lexer::token_kind_to_string(tk::arrow) == "->");
		assert(languages::cpp::lexer::token_kind_to_string(tk::tilde) == "~");
		assert(languages::cpp::lexer::token_kind_to_string(tk::operator_sym) == "operator");
		assert(languages::cpp::lexer::token_kind_to_string(tk::other) == "other");
		assert(languages::cpp::lexer::token_kind_to_string(static_cast<tk>(999)) == "unknown");
	}

	// 9. Preprocessor line continuation
	{
		std::string_view src = "#define FOO \\\n  42\nint y = FOO;";
		auto tokens = languages::cpp::lexer::tokenize(src);
		assert(tokens[0].kind == languages::cpp::token_kind::preprocessor);
		assert(tokens[0].text == "#define FOO \\\n  42");
		assert(tokens[1].kind == languages::cpp::token_kind::keyword);
		assert(tokens[1].text == "int");
		assert(tokens[1].line == 3);
	}

	// 10. String and character literal escaped newlines and unclosed char
	{
		std::string_view src = "\"line1\\\nline2\";\n'\\\n';\n'unclosed\nint after = 1;";
		auto tokens = languages::cpp::lexer::tokenize(src);
		assert(tokens[0].kind == languages::cpp::token_kind::string_literal);
		assert(tokens[0].text == "\"line1\\\nline2\"");
		assert(tokens[2].kind == languages::cpp::token_kind::character_literal);
		assert(tokens[2].text == "'\\\n'");

		bool found_after = false;
		for (const auto &tok : tokens) {
			if (tok.kind == languages::cpp::token_kind::identifier && tok.text == "after") {
				found_after = true;
				assert(tok.line == 6);
			}
		}
		assert(found_after);
	}

	// 11. Numbers: hex, float exponent, suffixes
	{
		std::string_view src = "0x1A 0x1.5p2 100ULL 3.14f";
		auto tokens = languages::cpp::lexer::tokenize(src);
		assert(tokens[0].kind == languages::cpp::token_kind::number);
		assert(tokens[0].text == "0x1A");
		assert(tokens[1].kind == languages::cpp::token_kind::number);
		assert(tokens[1].text == "0x1.5p2");
		assert(tokens[2].kind == languages::cpp::token_kind::number);
		assert(tokens[2].text == "100ULL");
		assert(tokens[3].kind == languages::cpp::token_kind::number);
		assert(tokens[3].text == "3.14f");
	}

	// 12. Three-way comparison operator <=> and unrecognized symbols
	{
		std::string_view src = "auto cmp = a <=> b; @ $";
		auto tokens = languages::cpp::lexer::tokenize(src);
		bool found_spaceship = false;
		int other_count = 0;
		for (const auto &tok : tokens) {
			if (tok.kind == languages::cpp::token_kind::operator_sym && tok.text == "<=>") {
				found_spaceship = true;
			}
			if (tok.kind == languages::cpp::token_kind::other) {
				++other_count;
			}
		}
		assert(found_spaceship);
		assert(other_count == 2);
	}

	std::cout << "All languages::cpp::lexer tests passed!" << std::endl;
	return 0;
}
