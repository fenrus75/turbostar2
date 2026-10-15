// Tested source file: src/markdown_utils.cpp
#include "test_watchdog.h"
#include <cassert>
#include <clocale>
#include <iostream>
#include <string>
#include <vector>
#include "markdown_utils.h"

using namespace markdown_utils;

void test_is_table_row()
{
	// Standard rows with outer pipes
	assert(table_aligner::is_table_row("| col 1 | col 2 |"));
	assert(table_aligner::is_table_row("|col1|col2|"));
	assert(table_aligner::is_table_row("  | col 1 | col 2 |  "));

	// Rows without outer pipes
	assert(table_aligner::is_table_row("col 1 | col 2"));
	assert(table_aligner::is_table_row("h1 | h2 | h3"));

	// Rows with escaped pipes
	assert(table_aligner::is_table_row("| a \\| b | c |"));

	// Negative cases
	assert(!table_aligner::is_table_row("just some text"));
	assert(!table_aligner::is_table_row(""));
	assert(!table_aligner::is_table_row("     "));
	assert(!table_aligner::is_table_row("|"));
	assert(!table_aligner::is_table_row("  |  "));
	assert(!table_aligner::is_table_row("| | |"));
	assert(!table_aligner::is_table_row("   |   |   "));
}

void test_is_header_separator()
{
	// Standard separators
	assert(table_aligner::is_header_separator("|---|---|"));
	assert(table_aligner::is_header_separator("---|---"));
	assert(table_aligner::is_header_separator("| --- | --- |"));

	// Alignment indicators
	assert(table_aligner::is_header_separator("| :--- | :---: | ---: |"));
	assert(table_aligner::is_header_separator(":---|:---:|---:"));
	assert(table_aligner::is_header_separator(" :--- | ---: "));

	// Minimal dashes
	assert(table_aligner::is_header_separator("|-|:-:|"));
	assert(table_aligner::is_header_separator("-|-"));

	// Negative cases
	assert(!table_aligner::is_header_separator("| col 1 | col 2 |"));
	assert(!table_aligner::is_header_separator("just text"));
	assert(!table_aligner::is_header_separator("---"));
	assert(!table_aligner::is_header_separator(""));
	assert(!table_aligner::is_header_separator("|"));
	assert(!table_aligner::is_header_separator("| === | === |"));
	assert(!table_aligner::is_header_separator("text with - and | in it"));
}

void test_find_table_ranges()
{
	// 1. Multiple tables separated by text
	std::vector<std::string> lines = {
		"text",
		"| h1 | h2 |",
		"|---|---|",
		"| c1 | c2 |",
		"more text",
		"a | b",
		"---|---",
		"c | d",
		"end"
	};
	auto ranges = table_aligner::find_table_ranges(lines);
	assert(ranges.size() == 2);
	assert(ranges[0].start_line == 1 && ranges[0].end_line == 3);
	assert(ranges[1].start_line == 5 && ranges[1].end_line == 7);

	// 2. Table at start of file (line 0)
	std::vector<std::string> start_lines = {
		"| Header A | Header B |",
		"|---|---|",
		"| Cell A | Cell B |",
		"trailing text"
	};
	auto start_ranges = table_aligner::find_table_ranges(start_lines);
	assert(start_ranges.size() == 1);
	assert(start_ranges[0].start_line == 0 && start_ranges[0].end_line == 2);

	// 3. Table at end of file
	std::vector<std::string> end_lines = {
		"leading text",
		"| Header A | Header B |",
		"|---|---|",
		"| Cell A | Cell B |"
	};
	auto end_ranges = table_aligner::find_table_ranges(end_lines);
	assert(end_ranges.size() == 1);
	assert(end_ranges[0].start_line == 1 && end_ranges[0].end_line == 3);

	// 4. Lines with pipes but NO header separator (must not be identified as a table)
	std::vector<std::string> no_sep_lines = {
		"line 1 | with pipe",
		"line 2 | with pipe",
		"line 3 without pipe"
	};
	auto no_sep_ranges = table_aligner::find_table_ranges(no_sep_lines);
	assert(no_sep_ranges.empty());

	// 5. Empty input
	assert(table_aligner::find_table_ranges({}).empty());
}

void test_align_table_block()
{
	// 1. Basic 3-column table
	std::vector<std::string> table = {
		"| Name | Age | City |",
		"|---|---|---|",
		"| Alice | 30 | New York |",
		"| Bob | 25 | London |",
		"| Charlie | 35 | San Francisco |"
	};

	auto aligned = table_aligner::align_table_block(table);
	assert(aligned.size() == 5);
	assert(aligned[0] == "| Name    | Age | City          |");
	assert(aligned[1] == "|---------|-----|---------------|");
	assert(aligned[2] == "| Alice   | 30  | New York      |");
	assert(aligned[3] == "| Bob     | 25  | London        |");
	assert(aligned[4] == "| Charlie | 35  | San Francisco |");

	// 2. Options: use_outer_pipes = false
	align_options no_outer;
	no_outer.use_outer_pipes = false;
	auto aligned_no_outer = table_aligner::align_table_block(table, no_outer);
	assert(aligned_no_outer.size() == 5);
	assert(aligned_no_outer[0] == " Name    | Age | City          ");
	assert(aligned_no_outer[1] == "---------|-----|---------------");
	assert(aligned_no_outer[2] == " Alice   | 30  | New York      ");
	align_options pad0;
	pad0.padding = 0;
	auto aligned_pad0 = table_aligner::align_table_block(table, pad0);
	assert(aligned_pad0.size() == 5);
	assert(aligned_pad0[0] == "|Name   |Age|City         |");
	assert(aligned_pad0[1] == "|-------|---|-------------|");
	assert(aligned_pad0[2] == "|Alice  |30 |New York     |");

	// 4. Options: custom padding (padding = 2)
	align_options pad2;
	pad2.padding = 2;
	auto aligned_pad2 = table_aligner::align_table_block(table, pad2);
	assert(aligned_pad2.size() == 5);
	assert(aligned_pad2[0] == "|  Name     |  Age  |  City           |");
	assert(aligned_pad2[1] == "|-----------|-------|-----------------|");
	assert(aligned_pad2[2] == "|  Alice    |  30   |  New York       |");

	// 5. Options: use_utf8_frames = true
	align_options framed;
	framed.use_utf8_frames = true;
	auto aligned_framed = table_aligner::align_table_block(table, framed);
	assert(aligned_framed.size() == 7);
	assert(aligned_framed[0] == "┌─────────┬─────┬───────────────┐");
	assert(aligned_framed[1] == "│ Name    │ Age │ City          │");
	assert(aligned_framed[2] == "├─────────┼─────┼───────────────┤");
	assert(aligned_framed[3] == "│ Alice   │ 30  │ New York      │");
	assert(aligned_framed[4] == "│ Bob     │ 25  │ London        │");
	assert(aligned_framed[5] == "│ Charlie │ 35  │ San Francisco │");
	assert(aligned_framed[6] == "└─────────┴─────┴───────────────┘");

	// 6. Uneven column counts across rows
	std::vector<std::string> uneven_table = {
		"| Col 1 | Col 2 | Col 3 |",
		"|---|---|---|",
		"| Value 1 |",
		"| A | B | C | Extra |"
	};
	auto aligned_uneven = table_aligner::align_table_block(uneven_table);
	assert(aligned_uneven.size() == 4);
	// Should expand missing columns to match maximum column count (4 columns)
	assert(aligned_uneven[0] == "| Col 1   | Col 2 | Col 3 |       |");
	assert(aligned_uneven[1] == "|---------|-------|-------|-------|");
	assert(aligned_uneven[2] == "| Value 1 |       |       |       |");
	assert(aligned_uneven[3] == "| A       | B     | C     | Extra |");

	// 7. Empty table input
	assert(table_aligner::align_table_block({}).empty());
}

void test_align_table_escaped_pipe()
{
	std::vector<std::string> table = {
		"| Operator | Description |",
		"|---|---|",
		"| \\| | Bitwise OR |",
		"| a \\| b | Either a or b |"
	};

	auto aligned = table_aligner::align_table_block(table);
	assert(aligned.size() == 4);
	// The aligned output MUST preserve the escaped pipe \| and NOT turn it into a bare | delimiter
	assert(aligned[2].find("\\|") != std::string::npos);
	assert(aligned[3].find("a \\| b") != std::string::npos);

	// Furthermore, aligning the already aligned table should be idempotent
	auto aligned_again = table_aligner::align_table_block(aligned);
	assert(aligned_again == aligned);
}

void test_align_table_utf8()
{
	// 1. Emoji visual width (2 columns each)
	std::vector<std::string> table = {
		"| Emoji | Meaning |",
		"|---|---|",
		"| 🦀 | Rust |",
		"| 🚀 | Fast |",
		"| 💻 | Code |"
	};

	auto aligned = table_aligner::align_table_block(table);
	assert(aligned.size() == 5);
	assert(aligned[0] == "| Emoji | Meaning |");
	assert(aligned[1] == "|-------|---------|");
	assert(aligned[2] == "| 🦀    | Rust    |");
	assert(aligned[3] == "| 🚀    | Fast    |");
	assert(aligned[4] == "| 💻    | Code    |");

	// 2. Multibyte CJK characters (2 columns display width each)
	std::vector<std::string> cjk_table = {
		"| 语言 | 描述 |",
		"|---|---|",
		"| 中文 | 汉语 |",
		"| 日本語 | 日本语 |"
	};
	auto aligned_cjk = table_aligner::align_table_block(cjk_table);
	assert(aligned_cjk.size() == 4);
	// "语言" = 4 visual width, "描述" = 4 visual width.
	// "日本語" = 6 visual width, "日本语" = 6 visual width.
	assert(aligned_cjk[0] == "| 语言   | 描述   |");
	assert(aligned_cjk[1] == "|--------|--------|");
	assert(aligned_cjk[2] == "| 中文   | 汉语   |");
	assert(aligned_cjk[3] == "| 日本語 | 日本语 |");
}

void test_align_table_min_max_width()
{
	std::vector<std::string> table = {"| Name | Age |", "|---|---|", "| Alice | 30 |", "| Bob | 25 |"};

	// 1. Minimum width expansion (natural width = 15, request 21)
	auto aligned1 = table_aligner::align_table_block(table, {}, 21, 0);
	assert(aligned1.size() == 4);
	assert(aligned1[0] == "| Name     | Age    |");
	assert(aligned1[1] == "|----------|--------|");
	assert(aligned1[2] == "| Alice    | 30     |");

	// 2. Minimum width expansion with overshoot capped by max_width
	auto aligned2 = table_aligner::align_table_block(table, {}, 22, 22);
	assert(aligned2.size() == 4);
	assert(aligned2[0] == "| Name      | Age    |");
	assert(aligned2[1] == "|-----------|--------|");
	assert(aligned2[2] == "| Alice     | 30     |");

	// 3. Maximum width shrinking and ellipsis truncation when word_wrap = false
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

	// 4. Narrow max_width (target_width <= 3 truncates without ellipsis)
	auto aligned_narrow = table_aligner::align_table_block(long_table, no_wrap_opts, 0, 15);
	assert(aligned_narrow.size() == 4);
	assert(aligned_narrow[0].length() <= 16);
}

void test_align_table_word_wrap()
{
	std::vector<std::string> long_table = {
		"| Name | Description |",
		"|---|---|",
		"| Alice | Software Engineer at Google DeepMind |",
		"| Bob | Designer |"
	};

	// 1. Standard pipe table word wrapping
	align_options wrap_opts;
	wrap_opts.word_wrap = true;
	auto aligned = table_aligner::align_table_block(long_table, wrap_opts, 0, 30);
	assert(aligned.size() == 5);
	assert(aligned[0] == "| Name  | Description        |");
	assert(aligned[1] == "|-------|--------------------|");
	assert(aligned[2] == "| Alice | Software Engineer  |");
	assert(aligned[3] == "|       | at Google DeepMind |");
	assert(aligned[4] == "| Bob   | Designer           |");

	// 2. Framed table word wrapping
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

	// 3. Single long word exceeding column width (splits at glyph boundaries)
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
		"| `fs_read` | ✅ Active |",
		"| `a **b** c` | Plain |"
	};

	auto aligned = table_aligner::align_table_block(table);
	assert(aligned.size() == 5);
	// **image** formatted visual width is 5
	// `fs_read` formatted visual width is 7
	// `a **b** c` inside code: ** is NOT stripped, formatted visual width is 9
	// Col 0 max width is 9 ("Tool" + 5 spaces = 9)
	// Col 1 max width is 9 ("✅ Active" = 2 + 7 = 9)
	assert(aligned[0] == "| Tool      | Status    |");
	assert(aligned[1] == "|-----------|-----------|");
	assert(aligned[2] == "| **image**     | ✅ Active |");
	assert(aligned[3] == "| `fs_read`   | ✅ Active |");
	assert(aligned[4] == "| `a **b** c` | Plain     |");
}

void test_display_width()
{
	assert(markdown_utils::display_width("") == 0);
	assert(markdown_utils::display_width("hello") == 5);
	assert(markdown_utils::display_width("café") == 4);
	assert(markdown_utils::display_width("🦀") == 2);
	assert(markdown_utils::display_width("中文") == 4);
	assert(markdown_utils::display_width("hello 🌍 world") == 14);
}

void test_align_all_tables()
{
	// 1. Text with no tables
	std::string no_tables = "# Title\n\nThis is a paragraph without any tables.\n- item 1\n- item 2\n";
	assert(markdown_utils::align_all_tables(no_tables) == no_tables);

	// 2. Empty string
	assert(markdown_utils::align_all_tables("") == "");

	// 3. Document with a single table surrounded by markdown content
	std::string doc =
		"# Header\n"
		"\n"
		"Paragraph before table.\n"
		"\n"
		"| Tool | Status |\n"
		"|---|---|\n"
		"| git | Active |\n"
		"| lsp | Idle |\n"
		"\n"
		"Paragraph after table.\n";

	std::string expected_doc =
		"# Header\n"
		"\n"
		"Paragraph before table.\n"
		"\n"
		"| Tool | Status |\n"
		"|------|--------|\n"
		"| git  | Active |\n"
		"| lsp  | Idle   |\n"
		"\n"
		"Paragraph after table.\n";

	assert(markdown_utils::align_all_tables(doc) == expected_doc);

	// 4. Document with multiple tables
	std::string multi_doc =
		"Table 1:\n"
		"| A | B |\n"
		"|---|---|\n"
		"| 1 | 2 |\n"
		"\n"
		"Between:\n"
		"\n"
		"| Long Column Header | Val |\n"
		"|---|---|\n"
		"| Short | V |\n";

	std::string expected_multi =
		"Table 1:\n"
		"| A | B |\n"
		"|---|---|\n"
		"| 1 | 2 |\n"
		"\n"
		"Between:\n"
		"\n"
		"| Long Column Header | Val |\n"
		"|--------------------|-----|\n"
		"| Short              | V   |\n";

	assert(markdown_utils::align_all_tables(multi_doc) == expected_multi);

	// 5. CRLF line endings
	std::string crlf_doc = "Header\r\n| a | b |\r\n|---|---|\r\n| 1 | 2 |\r\nFooter\r\n";
	std::string aligned_crlf = markdown_utils::align_all_tables(crlf_doc);
	assert(aligned_crlf.find("| a | b |") != std::string::npos);
	assert(aligned_crlf.find("| 1 | 2 |") != std::string::npos);

	// 6. Framed tables in document
	std::string table_only = "| h1 | h2 |\n|---|---|\n| a | b |\n";
	std::string framed_res = markdown_utils::align_all_tables(table_only, true);
	assert(framed_res.find("┌") != std::string::npos);
	assert(framed_res.find("├") != std::string::npos);
	assert(framed_res.find("└") != std::string::npos);

	// 7. Width constraints in align_all_tables
	std::string wide_table = "| Name | Description |\n|---|---|\n| Alice | Software Engineer at Google DeepMind |\n";
	std::string wrapped_res = markdown_utils::align_all_tables(wide_table, false, 0, 30, true);
	assert(wrapped_res.find("Software Engineer") != std::string::npos);
	assert(wrapped_res.find("at Google DeepMind") != std::string::npos);
}

void test_markdown_utils_coverage()
{
	// 1. Trailing backslash in tokenize_row
	std::vector<std::string> trailing_bs = {
		"| h1 | h2 \\",
		"|---|---|",
		"| a | b |"
	};
	auto aligned_bs = table_aligner::align_table_block(trailing_bs);
	assert(!aligned_bs.empty());

	// 2. Truncated/incomplete UTF-8 sequence in formatted_display_width and wrap_cell
	std::string bad_utf8_cell = "test\xC2";
	std::vector<std::string> bad_utf8_table = {
		"| Header | Other |",
		"|---|---|",
		"| " + bad_utf8_cell + " | normal |"
	};
	auto aligned_bad_utf8 = table_aligner::align_table_block(bad_utf8_table);
	assert(!aligned_bad_utf8.empty());
	std::vector<std::string> bad_utf8_wrap_table = {
		"| Header | Other |",
		"|---|---|",
		"| This is a very long string ending with a truncated character \xC2 | normal |"
	};
	align_options wrap_opt;
	wrap_opt.word_wrap = true;
	auto aligned_bad_utf8_wrap = table_aligner::align_table_block(bad_utf8_wrap_table, wrap_opt, 0, 15);
	assert(!aligned_bad_utf8_wrap.empty());

	// 3. Truncate cell with target_width <= 3 without wrapping
	align_options opts_nowrap;
	opts_nowrap.word_wrap = false;
	opts_nowrap.padding = 0;
	std::vector<std::string> long_cells = {
		"| LongHeader1 | LongHeader2 |",
		"|---|---|",
		"| SomeLongData | AnotherLongData |"
	};
	auto truncated_small = table_aligner::align_table_block(long_cells, opts_nowrap, 0, 7);
	assert(!truncated_small.empty());

	// 4. Wrap cell with target_width == 0 and extreme max_width
	align_options opts_wrap;
	opts_wrap.word_wrap = true;
	auto extreme_shrink = table_aligner::align_table_block(long_cells, opts_wrap, 0, 2);
	assert(!extreme_shrink.empty());

	// 5. Wrap cell with inline formatting: **bold** and `code`
	std::vector<std::string> formatted_wrap_table = {
		"| Column A | Column B |",
		"|---|---|",
		"| **This is bold text** that will be wrapped | Here is `some inline code` that spans across lines |"
	};
	auto wrapped_formatted = table_aligner::align_table_block(formatted_wrap_table, opts_wrap, 0, 30);
	assert(!wrapped_formatted.empty());

	// 6. Wrap cell with wide character that exceeds target_width (triggers fallback current_byte_len == 0 -> clen)
	std::vector<std::string> cjk_table = {
		"| A | B |",
		"|---|---|",
		"| 世界你好 | data |"
	};
	auto cjk_wrapped = table_aligner::align_table_block(cjk_table, opts_wrap, 0, 8);
	assert(!cjk_wrapped.empty());

	// 7. UTF-8 frames with word_wrap = false
	align_options opts_framed_nowrap;
	opts_framed_nowrap.use_utf8_frames = true;
	opts_framed_nowrap.word_wrap = false;
	std::vector<std::string> frame_table = {
		"| Col 1 | Col 2 |",
		"|---|---|",
		"| Value A | Value B |"
	};
	auto framed_nowrap_res = table_aligner::align_table_block(frame_table, opts_framed_nowrap);
	assert(!framed_nowrap_res.empty());
	assert(framed_nowrap_res[0].find("┌") != std::string::npos);
	assert(framed_nowrap_res.back().find("└") != std::string::npos);
	bool has_pipe = false;
	for (const auto &line : framed_nowrap_res) {
		if (line.find("│") != std::string::npos) {
			has_pipe = true;
			break;
		}
	}
	assert(has_pipe);
}

int main()
{
	test_watchdog::setup_watchdog(30);
	setlocale(LC_ALL, "");

	test_is_table_row();
	test_is_header_separator();
	test_find_table_ranges();
	test_align_table_block();
	test_align_table_escaped_pipe();
	test_align_table_utf8();
	test_align_table_min_max_width();
	test_align_table_word_wrap();
	test_align_table_formatted();
	test_display_width();
	test_align_all_tables();
	test_markdown_utils_coverage();

	std::cout << "All markdown_utils unit tests passed successfully!\n";
}
