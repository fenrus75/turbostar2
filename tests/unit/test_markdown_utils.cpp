#include "test_watchdog.h"
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

void test_align_table_min_max_width()
{
	std::vector<std::string> table = {"| Name | Age |", "|---|---|", "| Alice | 30 |", "| Bob | 25 |"};

	// 1. Test minimum width expansion (even expansion of both columns)
	// Natural width = 2 (pipes) + 1 (separator) + 4 (padding) + 5 (col1: Alice) + 3 (col2: Age) = 15.
	// We request min_width = 21.
	// needed = 21 - 15 = 6. extra_per_col = 3.
	// col1 width becomes 8, col2 width becomes 6.
	// Expected total width = 21.
	auto aligned1 = table_aligner::align_table_block(table, {}, 21, 0);
	assert(aligned1.size() == 4);
	assert(aligned1[0] == "| Name     | Age    |");
	assert(aligned1[1] == "|----------|--------|");
	assert(aligned1[2] == "| Alice    | 30     |");

	// 2. Test minimum width expansion with overshoot capped by max_width
	// We request min_width = 22.
	// needed = 22 - 15 = 7. extra_per_col = 4.
	// Without max_width, new_width = 15 + 8 = 23 (overshoot).
	// With max_width = 22, overshoot = 23 - 22 = 1.
	// Rightmost column (col2) expands one less (4 - 1 = 3).
	// col1 width becomes 5+4 = 9, col2 width becomes 3+3 = 6.
	// Expected total width = 22.
	auto aligned2 = table_aligner::align_table_block(table, {}, 22, 22);
	assert(aligned2.size() == 4);
	assert(aligned2[0] == "| Name      | Age    |");
	assert(aligned2[1] == "|-----------|--------|");
	assert(aligned2[2] == "| Alice     | 30     |");

	// 3. Test maximum width shrinking and ellipsis truncation when word_wrap = false
	std::vector<std::string> long_table = {
		"| Name | Description |",
		"|---|---|",
		"| Alice | Software Engineer at Google DeepMind |",
		"| Bob | Designer |"
	};
	align_options no_wrap_opts;
	no_wrap_opts.word_wrap = false;
	auto aligned3 = table_aligner::align_table_block(long_table, no_wrap_opts, 0, 30);
	assert(aligned3.size() == 4);
	assert(aligned3[0] == "| Name  | Description        |");
	assert(aligned3[1] == "|-------|--------------------|");
	assert(aligned3[2] == "| Alice | Software Engine... |");
	assert(aligned3[3] == "| Bob   | Designer           |");
}

void test_align_table_word_wrap()
{
	std::vector<std::string> long_table = {
		"| Name | Description |",
		"|---|---|",
		"| Alice | Software Engineer at Google DeepMind |",
		"| Bob | Designer |"
	};

	// 1. Test standard pipe table word wrapping (word_wrap = true)
	align_options wrap_opts;
	wrap_opts.word_wrap = true;
	auto aligned = table_aligner::align_table_block(long_table, wrap_opts, 0, 30);
	assert(aligned.size() == 5);
	assert(aligned[0] == "| Name  | Description        |");
	assert(aligned[1] == "|-------|--------------------|");
	assert(aligned[2] == "| Alice | Software Engineer  |");
	assert(aligned[3] == "|       | at Google DeepMind |");
	assert(aligned[4] == "| Bob   | Designer           |");

	// 2. Test framed table word wrapping
	align_options framed_wrap_opts;
	framed_wrap_opts.use_utf8_frames = true;
	framed_wrap_opts.word_wrap = true;
	auto framed_aligned = table_aligner::align_table_block(long_table, framed_wrap_opts, 0, 30);
	assert(framed_aligned.size() == 7);
	assert(framed_aligned[0] == "┌───────┬────────────────────┐");
	assert(framed_aligned[1] == "│ Name  │ Description        │");
	assert(framed_aligned[2] == "├───────┼────────────────────┤");
	assert(framed_aligned[3] == "│ Alice │ Software Engineer  │");
	assert(framed_aligned[4] == "│       │ at Google DeepMind │");
	assert(framed_aligned[5] == "│ Bob   │ Designer           │");
	assert(framed_aligned[6] == "└───────┴────────────────────┘");

	// 3. Test word wrapping when a single word exceeds the target column width (cuts word at max_width)
	std::vector<std::string> long_word_table = {
		"| Key | Value |",
		"|---|---|",
		"| Long | supercalifragilisticexpialidocious |"
	};
	auto long_word_aligned = table_aligner::align_table_block(long_word_table, wrap_opts, 0, 20);
	assert(long_word_aligned.size() == 6);
	assert(long_word_aligned[0] == "| Key  | Value     |");
	assert(long_word_aligned[1] == "|------|-----------|");
	assert(long_word_aligned[2] == "| Long | supercali |");
	assert(long_word_aligned[3] == "|      | fragilist |");
	assert(long_word_aligned[4] == "|      | icexpiali |");
	assert(long_word_aligned[5] == "|      | docious   |");
}


void test_align_table_formatted()
{
	std::vector<std::string> table = {
		"| Tool | Status |",
		"|---|---|",
		"| **image** | ✅ Active |",
		"| `fs_read` | ✅ Active |"
	};

	auto aligned = table_aligner::align_table_block(table);

	// "**image**" formatted visual width is 5.
	// "`fs_read`" formatted visual width is 7.
	// Max col 1 width is 7.
	// Status "✅ Active" visual width is 9 (2 emoji + 7 text).
	assert(aligned.size() == 4);
	assert(aligned[0] == "| Tool    | Status    |");
	assert(aligned[1] == "|---------|-----------|");
	assert(aligned[2] == "| **image**   | ✅ Active |");
	assert(aligned[3] == "| `fs_read` | ✅ Active |");
}

int main()
{
	test_watchdog::setup_watchdog(30);
	setlocale(LC_ALL, "");
	test_is_table_row();
	test_is_header_separator();
	test_find_table_ranges();
	test_align_table_block();
	test_align_table_utf8();
	test_align_table_min_max_width();
	test_align_table_word_wrap();
	test_align_table_formatted();

	std::cout << "markdown_utils unit tests passed!\n";
	return 0;
}

