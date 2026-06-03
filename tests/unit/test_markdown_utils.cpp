#include <cassert>
#include <iostream>
#include <vector>
#include <clocale>
#include "../../src/markdown_utils.h"

using namespace markdown_utils;

void test_is_table_row()
{
	assert(table_aligner::is_table_row("| col 1 | col 2 |"));
	assert(table_aligner::is_table_row("col 1 | col 2"));
	assert(!table_aligner::is_table_row("just some text"));
	assert(!table_aligner::is_table_row("  |  ")); // only pipes/whitespace
}

void test_is_header_separator()
{
	assert(table_aligner::is_header_separator("|---|---|"));
	assert(table_aligner::is_header_separator("---|---"));
	assert(table_aligner::is_header_separator("| :--- | :---: | ---: |"));
	assert(!table_aligner::is_header_separator("| col 1 | col 2 |"));
}

void test_find_table_ranges()
{
	std::vector<std::string> lines = {"text",  "| h1 | h2 |", "|---|---|", "| c1 | c2 |", "more text",
					  "a | b", "---|---",	  "c | d",     "end"};

	auto ranges = table_aligner::find_table_ranges(lines);
	assert(ranges.size() == 2);
	assert(ranges[0].start_line == 1);
	assert(ranges[0].end_line == 3);
	assert(ranges[1].start_line == 5);
	assert(ranges[1].end_line == 7);
}

void test_align_table_block()
{
	std::vector<std::string> table = {"| Name | Age | City |", "|---|---|---|", "| Alice | 30 | New York |", "| Bob | 25 | London |",
					  "| Charlie | 35 | San Francisco |"};

	auto aligned = table_aligner::align_table_block(table);

	assert(aligned.size() == 5);
	assert(aligned[0] == "| Name    | Age | City          |");
	assert(aligned[1] == "|---------|-----|---------------|");
	assert(aligned[2] == "| Alice   | 30  | New York      |");
}

void test_align_table_utf8()
{
	std::vector<std::string> table = {"| Emoji | Meaning |", "|---|---|", "| 🦀 | Rust |", "| 🚀 | Fast |", "| 💻 | Code |"};

	auto aligned = table_aligner::align_table_block(table);

	// Each emoji is 2 chars wide using display_width
	assert(aligned.size() == 5);
	assert(aligned[0] == "| Emoji | Meaning |");
	assert(aligned[1] == "|-------|---------|");
	assert(aligned[2] == "| 🦀    | Rust    |");
	assert(aligned[3] == "| 🚀    | Fast    |");
}

int main()
{
	setlocale(LC_ALL, "");
	test_is_table_row();
	test_is_header_separator();
	test_find_table_ranges();
	test_align_table_block();
	test_align_table_utf8();

	std::cout << "markdown_utils unit tests passed!\n";
	return 0;
}
