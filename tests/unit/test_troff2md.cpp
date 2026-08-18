#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <string>
#include "../../src/tools/troff2md.h"

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

	std::cout << "troff2md unit test passed!\n";
	return 0;
}
