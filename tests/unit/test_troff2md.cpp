// Tested source file: src/tools/troff2md.cpp
#include "test_watchdog.h"

#include <cassert>
#include <iostream>
#include <string>
#include "tools/troff2md.h"

int main()
{
	test_watchdog::setup_watchdog(30);

	// Test 1: Basic headers and font formatting
	{
		std::string input =
		    ".TH LS 1 \"2026-08-17\" \"GNU coreutils\" \"User Commands\"\n"
		    ".SH NAME\n"
		    "ls \\- list directory contents\n"
		    ".SH SYNOPSIS\n"
		    ".B ls\n"
		    "[\\fIOPTION\\fR]... [\\fIFILE\\fR]...\n"
		    ".SH DESCRIPTION\n"
		    "List information about the FILEs (the current directory by default).\n";

		std::string md = troff2md(input);
		assert(!md.empty());
		assert(md.find("NAME") != std::string::npos);
		assert(md.find("SYNOPSIS") != std::string::npos);
		assert(md.find("DESCRIPTION") != std::string::npos);
		assert(md.find("ls") != std::string::npos);
	}

	// Test 2: Tagged paragraphs (.TP) and alternating macros (.BR, .IR, .BI)
	{
		std::string input =
		    ".SH OPTIONS\n"
		    ".TP\n"
		    ".BR \\-a \", \" \\-\\-all\n"
		    "do not ignore entries starting with .\n"
		    ".TP\n"
		    ".BI \\-l \" format\"\n"
		    "use a long listing format\n";

		std::string md = troff2md(input);
		assert(md.find("OPTIONS") != std::string::npos);
		assert(md.find("do not ignore entries") != std::string::npos);
		assert(md.find("long listing format") != std::string::npos);
	}

	// Test 3: Indented blocks (.RS / .RE) and paragraph breaks (.PP / .P)
	{
		std::string input =
		    ".SH EXAMPLES\n"
		    ".PP\n"
		    "First example paragraph.\n"
		    ".RS\n"
		    "Indented code block or details\n"
		    ".RE\n"
		    ".P\n"
		    "Second paragraph after indent.\n";

		std::string md = troff2md(input);
		assert(md.find("EXAMPLES") != std::string::npos);
		assert(md.find("First example paragraph.") != std::string::npos);
		assert(md.find("Indented code block") != std::string::npos);
		assert(md.find("Second paragraph after indent.") != std::string::npos);
	}

	// Test 4: Comment line filtering
	{
		std::string input =
		    ".\\\" This is a troff comment line\n"
		    ".SH NOTES\n"
		    "Visible note text.\n";

		std::string md = troff2md(input);
		assert(md.find("This is a troff comment line") == std::string::npos);
		assert(md.find("Visible note text.") != std::string::npos);
	}

	// Test 5: Negative indent bug / invalid number in .in and .RS
	{
		std::string input =
		    ".in +-5\n"
		    "Text after negative indent.\n"
		    ".RS +-10\n"
		    "Text after negative RS.\n";
		std::string md = troff2md(input);
		assert(!md.empty());
		assert(md.find("Text after negative indent.") != std::string::npos);
		assert(md.find("Text after negative RS.") != std::string::npos);
	}

	// Test 6: Extreme numbers and overflow prevention in .in and .RS
	{
		std::string input =
		    ".in 2147483647\n"
		    "Huge indent line.\n"
		    ".in +999999999\n"
		    "Relative huge indent.\n"
		    ".in -2147483648\n"
		    "Underflow indent line.\n"
		    ".RS 10000000\n"
		    "Huge RS indent.\n"
		    ".in\n"
		    "Reset indent.\n";
		std::string md = troff2md(input);
		assert(md.find("Huge indent line.") != std::string::npos);
		assert(md.find("Reset indent.") != std::string::npos);
	}

	// Test 7: Deep nested .RS / .RE stack attack
	{
		std::string input;
		for (int i = 0; i < 200; ++i) {
			input += ".RS 2\nLine in deep nest\n";
		}
		for (int i = 0; i < 300; ++i) {
			input += ".RE\n";
		}
		input += "Back to root\n";
		std::string md = troff2md(input);
		assert(md.find("Back to root") != std::string::npos);
	}

	// Test 8: Deep nested .EX / .EE / .nf / .fi blocks and unclosed code block at EOF
	{
		std::string input =
		    ".EX\n"
		    "int code1();\n"
		    ".nf\n"
		    "int code2();\n"
		    ".EE\n"
		    "int code3();\n"
		    ".fi\n"
		    "regular text\n";
		for (int i = 0; i < 50; ++i) {
			input += ".fi\n.EE\n"; // excessive closes
		}
		input += ".EX\nunclosed code block\n";
		std::string md = troff2md(input);
		assert(md.find("int code1();") != std::string::npos);
		assert(md.find("unclosed code block") != std::string::npos);
		assert(md.find("```") != std::string::npos);
	}

	// Test 9: Unclosed .TS table at EOF (should flush remaining table data)
	{
		std::string input =
		    ".TS\n"
		    "allbox tab(;);\n"
		    "l l.\n"
		    "Key;Value\n"
		    "Alpha;Beta\n";
		std::string md = troff2md(input);
		assert(md.find("Key") != std::string::npos);
		assert(md.find("Beta") != std::string::npos);
	}

	// Test 10: Table formatting with pipe characters '|' in cells and delimiter variations
	{
		std::string input =
		    ".TS\n"
		    "allbox tab(;);\n"
		    "l l l.\n"
		    "Header1;Header2;Header3\n"
		    "Cell | with | pipes;Simple;Extra\n"
		    ".TE\n";
		std::string md = troff2md(input);
		assert(md.find("Cell \\| with \\| pipes") != std::string::npos);
	}

	// Test 11: Complex table with tab() options and format lines
	{
		std::string input =
		    ".TS\n"
		    "tab(@);\n"
		    "s s\n"
		    "l l.\n"
		    "Col1@Col2\n"
		    "Val1@Val2\n"
		    ".TE\n";
		std::string md = troff2md(input);
		assert(md.find("Col1") != std::string::npos);
		assert(md.find("Val2") != std::string::npos);
	}

	// Test 12: Malformed and dangling escape sequences
	{
		std::string input =
		    "Dangling backslash \\\n"
		    "Dangling font switch \\f\n"
		    "Dangling font without code \\f\n"
		    "Dangling paren \\(\n"
		    "Dangling single char paren \\(a\n"
		    "Escaped e \\e\n"
		    "Escaped minus \\-\n"
		    "Zero-width \\&\n"
		    "Valid font \\fBbold\\fR normal\n";
		std::string md = troff2md(input);
		assert(!md.empty());
		assert(md.find("**bold**") != std::string::npos);
	}

	// Test 13: Special characters \(aq, \(dq, \(bu, \(co, \(em, \(en, and unknown \(xy
	{
		std::string input =
		    "Quotes: \\(aq single \\(dq double\n"
		    "Bullet: \\(bu\n"
		    "Copyright: \\(co\n"
		    "Dashes: \\(em em-dash \\(en en-dash\n"
		    "Unknown: \\(xy\n";
		std::string md = troff2md(input);
		assert(md.find("' single \" double") != std::string::npos);
		assert(md.find("©") != std::string::npos);
		assert(md.find("— em-dash – en-dash") != std::string::npos);
		assert(md.find("\\(xy") != std::string::npos);
	}

	// Test 14: Non-ASCII and UTF-8 multi-byte characters
	{
		std::string input =
		    ".SH НАЗВАНИЕ\n"
		    "Привет, мир! 🚀\n"
		    ".B \"日本語のテスト\"\n"
		    ".IP \"•\" 2\n"
		    "Bullet with unicode\n";
		std::string md = troff2md(input);
		assert(md.find("НАЗВАНИЕ") != std::string::npos);
		assert(md.find("Привет, мир!") != std::string::npos);
		assert(md.find("日本語のテスト") != std::string::npos);
	}

	// Test 15: Alternating macros with unclosed quotes and empty arguments
	{
		std::string input =
		    ".BR \"unclosed quote\n"
		    ".BI \"\" \"\"\n"
		    ".IR a b c d e\n"
		    ".RB prefix ( suffix )\n"
		    ".IB italic bold\n";
		std::string md = troff2md(input);
		assert(!md.empty());
		assert(md.find("unclosed quote") != std::string::npos);
	}

	// Test 16: Edge cases: empty input, all newlines, CRLF, unhandled macros
	{
		assert(troff2md("").empty());
		assert(troff2md("\n\n\n").find("xyz") == std::string::npos);
		std::string crlf_input = ".SH TITLE\r\nSome text\r\n";
		std::string md = troff2md(crlf_input);
		assert(md.find("TITLE") != std::string::npos);
		assert(md.find("Some text") != std::string::npos);

		std::string unhandled = ".CUSTOM_MACRO arg1 arg2\nRegular line\n";
		std::string md_unhandled = troff2md(unhandled);
		assert(md_unhandled.find("CUSTOM_MACRO") != std::string::npos);
	}

	// Test 17: .ft font macro, .SS subsections, .I italics, .RI alternating
	{
		std::string input =
		    ".SH SECTION\n"
		    ".SS SUBSECTION\n"
		    ".ft B\n"
		    "Bold text via ft\n"
		    ".ft 3\n"
		    "Bold text via ft 3\n"
		    ".ft I\n"
		    "Italic text via ft\n"
		    ".ft 2\n"
		    "Italic text via ft 2\n"
		    ".ft R\n"
		    "Roman text\n"
		    ".ft unknown\n"
		    ".I italic_word\n"
		    ".RI \"roman_word\" \"italic_word\"\n"
		    ".EX\n"
		    ".I italic_in_fill\n"
		    ".EE\n";
		std::string md = troff2md(input);
		assert(md.find("## SUBSECTION") != std::string::npos);
		assert(md.find("*italic_word*") != std::string::npos);
		assert(md.find("italic_in_fill") != std::string::npos);
	}

	// Test 18: .TQ tags, empty/bullet .IP, input without trailing newline
	{
		std::string input =
		    ".SH COMMANDS\n"
		    ".TP\n"
		    ".B tag1\n"
		    ".TQ\n"
		    ".B tag2\n"
		    "Description for both tags\n"
		    ".IP\n"
		    "Paragraph item without tag\n"
		    ".IP \"\"\n"
		    "Paragraph item with empty tag\n"
		    "No trailing newline";
		std::string md = troff2md(input);
		assert(md.find("tag1") != std::string::npos);
		assert(md.find("tag2") != std::string::npos);
		assert(md.find("Description for both tags") != std::string::npos);
		assert(md.find("No trailing newline") != std::string::npos);
	}

	// Test 19: Relative indent deltas (.RS +4, .RS -2, .in +4, .in -2, .in 0) and stack underflow
	{
		std::string input =
		    ".RS +4\n"
		    "Indented by 4\n"
		    ".RS -2\n"
		    "Indented by 2\n"
		    ".in +6\n"
		    "Indented by 8\n"
		    ".in -4\n"
		    "Indented by 4\n"
		    ".in 0\n"
		    "Indented by 0\n"
		    ".RE\n"
		    ".RE\n"
		    ".RE\n" // Stack underflow test
		    "Back to margin\n";
		std::string md = troff2md(input);
		assert(!md.empty());
		assert(md.find("Back to margin") != std::string::npos);
	}

	// Test 20: Complex tables, T{ multiline cells, separators, comments, uneven columns
	{
		std::string input =
		    ".TS\n"
		    "allbox tab(;);\n"
		    "l c r.\n"
		    "Col1;Col2;Col3\n"
		    "_\n"
		    "=\n"
		    ".\\\" table comment\n"
		    "val1;val2;val3;\n" // Trailing delimiter creates empty col
		    "short_row\n"        // Fewer columns than header
		    "data with | pipe;col2;col3\n"
		    ".TE\n"
		    ".TS\n"
		    "l l.\n"
		    "Multi\tT{\n"        // Complex table with T{
		    "line\n"
		    "cell\n"
		    "T}\n"
		    ".TE\n";
		std::string md = troff2md(input);
		assert(!md.empty());
		assert(md.find("Col1") != std::string::npos);
		assert(md.find("short_row") != std::string::npos);
		assert(md.find("Multi") != std::string::npos);
	}

	// Test 21: Malicious & fuzzing robustness
	{
		// Deeply nested .RS (exceeding max depth 64)
		std::string deep_nesting;
		for (int i = 0; i < 100; ++i) {
			deep_nesting += ".RS +2\nText level\n";
		}
		for (int i = 0; i < 120; ++i) {
			deep_nesting += ".RE\n";
		}
		std::string md_deep = troff2md(deep_nesting);
		assert(!md_deep.empty());

		// Enormous indent deltas
		std::string extreme_in = ".in +99999999\nExtreme text\n.in -99999999\nReset text\n.RS +99999999\nDeep\n.RS -99999999\n";
		std::string md_extreme = troff2md(extreme_in);
		assert(!md_extreme.empty());

		// Truncated escape sequences
		std::string trunc_escapes =
		    "Trailing single backslash \\\n"
		    "Trailing font escape \\f\n"
		    "Trailing font paren \\f(\n"
		    "Trailing font bracket \\f[\n"
		    "Trailing size \\s+\n"
		    "Trailing string \\*\n"
		    "Trailing char \\(\n";
		std::string md_escapes = troff2md(trunc_escapes);
		assert(!md_escapes.empty());

		// Unterminated table at EOF
		std::string unclosed_ts =
		    ".SH TABLE\n"
		    ".TS\n"
		    "l r.\n"
		    "a\tb\n"
		    "c\td\n";
		std::string md_ts = troff2md(unclosed_ts);
		assert(md_ts.find("a") != std::string::npos);
		assert(md_ts.find("d") != std::string::npos);
	}

	// Test 22: Macros in literal blocks (.EX), complex table separators, solitary macros
	{
		std::string input =
		    ".EX\n"
		    ".B bold_in_literal\n"
		    ".BR alt1 alt2\n"
		    ".EE\n"
		    ".IP\n"
		    "Item under empty IP\n"
		    ".TS\n"
		    "l l.\n"
		    "_\n"
		    ".\\\" comment in tab table\n"
		    "Row1\tT{\n"
		    "cell\n"
		    "T}\n"
		    ".TE\n"
		    ".PP\n"
		    "Paragraph after empty macro\n";
		std::string md = troff2md(input);
		assert(!md.empty());
		assert(md.find("bold_in_literal") != std::string::npos);
		assert(md.find("alt1") != std::string::npos);
	}

	std::cout << "troff2md unit test passed!\n";
}
