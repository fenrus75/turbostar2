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

	std::cout << "troff2md unit test passed!\n";
	return 0;
}
