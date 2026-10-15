// Tested source file: src/utf8.cpp
#include "test_watchdog.h"

#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <clocale>
#include "utf8.h"

void test_utf8_char_len()
{
	assert(utf8::char_len('a') == 1);
	// 2-byte leading byte (0xC0 mask)
	assert(utf8::char_len(0xC0) == 2);
	assert(utf8::char_len(0xDF) == 2);
	// 3-byte leading byte (0xE0 mask)
	assert(utf8::char_len(0xE0) == 3);
	assert(utf8::char_len(0xEF) == 3);
	// 4-byte leading byte (0xF0 mask)
	assert(utf8::char_len(0xF0) == 4);
	assert(utf8::char_len(0xF7) == 4);
}

void test_utf8_length()
{
	assert(utf8::length("abc") == 3);
	assert(utf8::length("") == 0);
	// "🦀" is 4 bytes, "abc" is 3 bytes, total 4 characters
	assert(utf8::length("🦀abc") == 4);
	// UTF-8 flag emoji "🇺🇸" is composed of 2 regional indicator characters (each 4 bytes, total 8 bytes)
	assert(utf8::length("🇺🇸") == 2);
}

void test_utf8_char_to_byte_offset()
{
	std::string s = "🦀abc";
	assert(utf8::char_to_byte_offset(s, 0) == 0);
	assert(utf8::char_to_byte_offset(s, 1) == 4); // After "🦀" (4 bytes)
	assert(utf8::char_to_byte_offset(s, 2) == 5); // After "🦀a"
	assert(utf8::char_to_byte_offset(s, 4) == 7); // After whole string
	assert(utf8::char_to_byte_offset(s, 10) == 7); // Out of bounds fallback
}

void test_utf8_byte_to_char_pos()
{
	std::string s = "🦀abc";
	assert(utf8::byte_to_char_pos(s, 0) == 0);
	assert(utf8::byte_to_char_pos(s, 1) == 1); // inside first char -> maps to 1
	assert(utf8::byte_to_char_pos(s, 4) == 1); // exactly after first char
	assert(utf8::byte_to_char_pos(s, 5) == 2); // exactly after "🦀a"
	assert(utf8::byte_to_char_pos(s, 7) == 4);
	assert(utf8::byte_to_char_pos(s, 100) == 4);
}

void test_utf8_next_character()
{
	std::string s = "🦀a";
	size_t offset = 0;
	std::string c;

	assert(utf8::next_character(s, offset, c));
	assert(offset == 4);
	assert(c == "🦀");

	assert(utf8::next_character(s, offset, c));
	assert(offset == 5);
	assert(c == "a");

	assert(!utf8::next_character(s, offset, c));
}

void test_utf8_wrap_string()
{
	// 1. Basic wrapping with no prefix
	std::string text1 = "Quick brown fox jumps over the lazy dog";
	// Available width = 15. Wraps at spaces.
	auto lines1 = utf8::wrap_string("", text1, 15);
	assert(lines1.size() == 3);
	assert(lines1[0] == "Quick brown fox");
	assert(lines1[1] == "jumps over the");
	assert(lines1[2] == "lazy dog");

	// 2. Wrapping with a prefix (e.g. comment characters)
	std::string text2 = "This is a comment line that should wrap nicely.";
	// Prefix = "// ", width = 25. prefix_len = 3. available = 22.
	auto lines2 = utf8::wrap_string("// ", text2, 25);
	assert(lines2.size() == 3);
	assert(lines2[0] == "// This is a comment line");
	assert(lines2[1] == "   that should wrap");
	assert(lines2[2] == "   nicely.");

	// 3. Wrapping with UTF-8 double-width characters
	std::string text3 = "螃蟹 螃蟹 螃蟹 螃蟹 螃蟹"; // each 🦀/Chinese char is width 2
	// Width = 11. Prefix = "* ". prefix_len = 2. available = 9.
	// "螃蟹" is display width 4. "螃蟹 螃蟹" is display width 9.
	auto lines3 = utf8::wrap_string("* ", text3, 11);
	assert(lines3.size() == 3);
	assert(lines3[0] == "* 螃蟹 螃蟹");
	assert(lines3[1] == "  螃蟹 螃蟹");
	assert(lines3[2] == "  螃蟹");
}

void test_utf8_ascii_fastpath()
{
	std::string_view ascii_str = "Hello, Turbostar Editor 2026!";
	assert(utf8::display_width(ascii_str) == ascii_str.length());
	assert(utf8::length(ascii_str) == ascii_str.length());
	assert(utf8::is_valid_utf8(ascii_str));

	std::string_view mixed_str = "Hello 🦀 World!";
	assert(utf8::display_width(mixed_str) == 15); // 6 ASCII + 2 for crab emoji + 7 ASCII
	assert(utf8::length(mixed_str) == 14); // 6 ASCII + 1 crab emoji + 7 ASCII
}

void test_utf8_sanitize_and_validation()
{
	// Valid UTF-8
	std::string valid = "Hello 🦀 World";
	assert(utf8::is_valid_utf8(valid));
	assert(utf8::sanitize(valid) == valid);

	// Invalid UTF-8 bytes
	std::string invalid = "Hello \xFF\xFE World";
	assert(!utf8::is_valid_utf8(invalid));
	std::string sanitized = utf8::sanitize(invalid);
	assert(sanitized.find("?") != std::string::npos);

	// Embedded NUL byte
	std::string nul_str = std::string("Hello\0World", 11);
	assert(!utf8::is_valid_utf8(nul_str));
}

void test_utf8_trim_and_terminal_sanitize()
{
	// trim
	std::string s = "   hello world   \n\t";
	assert(utf8::trim(s) == "hello world");

	std::string s_trailing = "hello world  \n";
	utf8::trim_trailing_whitespace(s_trailing);
	assert(s_trailing == "hello world");

	// sanitize_terminal_output (ANSI codes removal)
	std::string ansi_colored = "\x1b[31mRed Error\x1b[0m Normal Text";
	assert(utf8::sanitize_terminal_output(ansi_colored) == "Red Error Normal Text");

	// Test incomplete trailing escape sequence is stripped, not leaked
	std::string trailing_esc = "Program received signal SIGSEGV\x1b[";
	assert(utf8::sanitize_terminal_output(trailing_esc) == "Program received signal SIGSEGV");

	std::string trailing_esc_alone = "Program received signal SIGSEGV\x1b";
	assert(utf8::sanitize_terminal_output(trailing_esc_alone) == "Program received signal SIGSEGV");

	std::string trailing_esc_csi = "Program received signal SIGSEGV\x1b[31";
	assert(utf8::sanitize_terminal_output(trailing_esc_csi) == "Program received signal SIGSEGV");

	// detect_mime
	std::string plain_text = "Plain text buffer content\n";
	std::string detected = utf8::detect_mime(plain_text);
	assert(!detected.empty());
}

void check_case_idempotence(std::string_view s)
{
	std::string snake = utf8::to_snake_case(s);
	if (snake.empty()) {
		assert(utf8::to_PascalCase(snake).empty());
		assert(utf8::to_camelCase(snake).empty());
		assert(utf8::to_snake_case(snake).empty());
		return;
	}

	using conv_fn = std::string (*)(std::string_view);
	struct op_entry {
		std::string_view name;
		conv_fn fn;
	};

	const std::vector<op_entry> ops = {
		{"snake", utf8::to_snake_case},
		{"pascal", utf8::to_PascalCase},
		{"camel", utf8::to_camelCase},
	};

	// Depth 1: to_snake_case(op1(snake)) == snake
	for (const auto &op1 : ops) {
		std::string mid = op1.fn(snake);
		std::string back = utf8::to_snake_case(mid);
		if (back != snake) {
			std::cerr << "Depth 1 case idempotence failure for input '" << s << "':\n"
				  << "  snake: '" << snake << "'\n"
				  << "  after " << op1.name << ": '" << mid << "'\n"
				  << "  back to snake: '" << back << "' (expected '" << snake << "')\n";
			assert(back == snake);
		}
	}

	// Depth 2: to_snake_case(op2(op1(snake))) == snake
	for (const auto &op1 : ops) {
		for (const auto &op2 : ops) {
			std::string mid1 = op1.fn(snake);
			std::string mid2 = op2.fn(mid1);
			std::string back = utf8::to_snake_case(mid2);
			if (back != snake) {
				std::cerr << "Depth 2 case idempotence failure for input '" << s << "':\n"
					  << "  snake: '" << snake << "'\n"
					  << "  after " << op1.name << ": '" << mid1 << "'\n"
					  << "  after " << op2.name << ": '" << mid2 << "'\n"
					  << "  back to snake: '" << back << "' (expected '" << snake << "')\n";
				assert(back == snake);
			}
		}
	}

	// PascalCase idempotence: to_PascalCase(to_PascalCase(snake)) == to_PascalCase(snake)
	// and to_PascalCase(to_camelCase(snake)) == to_PascalCase(snake)
	std::string pascal = utf8::to_PascalCase(snake);
	assert(utf8::to_PascalCase(pascal) == pascal);
	assert(utf8::to_PascalCase(utf8::to_camelCase(snake)) == pascal);
	assert(utf8::to_pascal_case(snake) == pascal);

	// camelCase idempotence: to_camelCase(to_camelCase(snake)) == to_camelCase(snake)
	// and to_camelCase(to_PascalCase(snake)) == to_camelCase(snake)
	std::string camel = utf8::to_camelCase(snake);
	assert(utf8::to_camelCase(camel) == camel);
	assert(utf8::to_camelCase(utf8::to_PascalCase(snake)) == camel);
	assert(utf8::to_camel_case(snake) == camel);
}

void test_utf8_case_conversions()
{
	// to_snake_case
	assert(utf8::to_snake_case("") == "");
	assert(utf8::to_snake_case("path") == "path");
	assert(utf8::to_snake_case("start_line") == "start_line");
	assert(utf8::to_snake_case("StartLine") == "start_line");
	assert(utf8::to_snake_case("startLine") == "start_line");
	assert(utf8::to_snake_case("WaitMsBeforeAsync") == "wait_ms_before_async");
	assert(utf8::to_snake_case("URL") == "url");
	assert(utf8::to_snake_case("HTMLParser") == "html_parser");
	assert(utf8::to_snake_case("newX") == "new_x");
	assert(utf8::to_snake_case("utf8Char") == "utf8_char");
	assert(utf8::to_snake_case("kebab-case-name") == "kebab_case_name");

	// to_PascalCase
	assert(utf8::to_PascalCase("") == "");
	assert(utf8::to_PascalCase("path") == "Path");
	assert(utf8::to_PascalCase("start_line") == "StartLine");
	assert(utf8::to_PascalCase("startLine") == "StartLine");
	assert(utf8::to_PascalCase("StartLine") == "StartLine");
	assert(utf8::to_PascalCase("wait_ms_before_async") == "WaitMsBeforeAsync");
	assert(utf8::to_pascal_case("target_content") == "TargetContent");

	// to_camelCase
	assert(utf8::to_camelCase("") == "");
	assert(utf8::to_camelCase("path") == "path");
	assert(utf8::to_camelCase("start_line") == "startLine");
	assert(utf8::to_camelCase("StartLine") == "startLine");
	assert(utf8::to_camelCase("startLine") == "startLine");
	assert(utf8::to_camelCase("wait_ms_before_async") == "waitMsBeforeAsync");
	assert(utf8::to_camel_case("target_content") == "targetContent");

	// format_markdown_table_header
	utf8::set_active_param_case_style(utf8::param_case_style::snake_case);
	assert(utf8::format_markdown_table_header("| Symbol | start_line | end_line | lines |\n") ==
	       "| Symbol | start_line | end_line | lines |\n");

	utf8::set_active_param_case_style(utf8::param_case_style::pascal_case);
	assert(utf8::format_markdown_table_header("| Symbol | start_line | end_line | lines |\n") ==
	       "| Symbol | StartLine | EndLine | Lines |\n");
	assert(utf8::format_markdown_table_header("| Symbol | path | start_line | end_line |\n") ==
	       "| Symbol | Path | StartLine | EndLine |\n");
	assert(utf8::format_markdown_table_header("| Type | Kind | path | start_line | end_line |\n") ==
	       "| Type | Kind | Path | StartLine | EndLine |\n");

	utf8::set_active_param_case_style(utf8::param_case_style::camel_case);
	assert(utf8::format_markdown_table_header("| Symbol | start_line | end_line | lines |\n") ==
	       "| Symbol | startLine | endLine | lines |\n");
	assert(utf8::format_markdown_table_header("| Type | Kind | path | start_line | end_line |\n") ==
	       "| Type | Kind | path | startLine | endLine |\n");

	utf8::set_active_param_case_style(utf8::param_case_style::snake_case);
}

void test_utf8_case_idempotence()
{
	const std::vector<std::string_view> test_strings = {
		"",
		"a",
		"x",
		"word",
		"Word",
		"start_line",
		"StartLine",
		"startLine",
		"end_line",
		"EndLine",
		"endLine",
		"path",
		"Path",
		"file_path",
		"target_content",
		"TargetContent",
		"targetContent",
		"replacement_content",
		"ReplacementContent",
		"replacementContent",
		"wait_ms_before_async",
		"WaitMsBeforeAsync",
		"waitMsBeforeAsync",
		"is_daemon",
		"IsDaemon",
		"isDaemon",
		"already_snake_case",
		"camelCase",
		"PascalCase",
		"kebab-case-string",
		"SCREAMING_SNAKE_CASE",
		"with spaces in name",
		"mix-of_delimiters and_styles",
		"multi_word_long_identifier_name",
		"alpha_beta_gamma_delta",
		// Acronyms
		"URL",
		"url",
		"Url",
		"HTMLParser",
		"html_parser",
		"HtmlParser",
		"htmlParser",
		// Numbers
		"utf8_char",
		"utf8Char",
		"Utf8Char",
		"v2_engine",
		"v2Engine",
		"V2Engine",
		"run3_step",
		"run3Step",
		"Run3Step",
		"sha256_hash",
		"sha256Hash",
		"Sha256Hash",
		"base64_encode",
		"base64Encode",
		"Base64Encode",
		"mp3_player",
		"mp3Player",
		"Mp3Player",
		"iso9660_filesystem",
		"iso9660Filesystem",
		"Iso9660Filesystem",
		"plan9_protocol",
		"plan9Protocol",
		"Plan9Protocol",
		"h264_decoder",
		"h264Decoder",
		"H264Decoder",
		"model3_revision2",
		"model3Revision2",
		"Model3Revision2",
		"item1",
		"Item1",
		"test123",
		"Test123",
		"step1_alpha",
		"alpha2_beta3",
		"arg0",
		"param1",
		"layer2_norm",
		"win32_api",
		"ipv4_address",
		"ipv6_socket",
		"oauth2_token",
		"bfloat16_tensor",
		"fp32_precision",
		"int64_value",
		"uint32_count",
		"tls13_handshake",
		"http2_stream",
		"ec2_instance",
		"s3_bucket",
		"x86_64",
		"step_1",
		"item_2_prop_3",
		"level_99_boss",
		"p50_latency",
		"p99_latency",
		"v1_2_3",
		"port_8080",
		"width_1080p",
		"3d_model",
		"2_factor_auth",
		"100_percent",
		"4k_video",
		"pos0_x",
		"pos1_y",
		"pos2_z",
		"PascalCaseWithNumbers123",
		"camelCaseWithNumbers456",
		"UPPER_CASE_WITH_NUMBERS_789",
		// UTF-8
		"café_au_lait",
		"café_noir",
		"münchen_airport",
		"résumé_data",
		"déjà_vu",
		"cliché_story",
		"fiancé_status",
		"attaché_case",
		"naïve_bayes",
		"año_nuevo",
		"château_rouge",
		"über_file",
		"crème_brûlée_flavor",
		"piñata_hit",
		"jalapeño_pepper",
		"habanero_sauce",
		"façade_pattern",
		"coöperation_token",
		"grüße_aus_berlin",
		"smörgåsbord_table",
		"vis-à-vis_mode",
		"maître_d",
		"zaïre_currency",
		"côte_d_azur",
		"håkan_name",
		"são_paulo",
		"brötchen_bread",
		"tête_à_tête",
		"hello_world_螃蟹",
		"螃蟹_item",
		"漢字_name",
		"東京_tower",
		"русский_текст",
		"привет_мир",
		"москва_сити",
		"greek_Ω_omega",
		"math_π_pi",
		"alpha_β_gamma",
		"crab_🦀_test",
		"rocket_🚀_launch",
		"sparkles_✨_effect",
		"fire_🔥_event",
		// Delimiter edge cases
		"multiple__consecutive___underscores",
		"trailing_underscore_",
		"_leading_underscore",
		"__leading_and_trailing__",
		"mixed---delimiters___test",
		"kebab-and-snake_case-mix",
		"spaces  and   tabs",
	};

	for (std::string_view s : test_strings) {
		check_case_idempotence(s);
	}
}

int main()
{
	test_watchdog::setup_watchdog(30);
	std::setlocale(LC_ALL, "");
	test_utf8_char_len();
	test_utf8_length();
	test_utf8_char_to_byte_offset();
	test_utf8_byte_to_char_pos();
	test_utf8_next_character();
	test_utf8_wrap_string();
	test_utf8_ascii_fastpath();
	test_utf8_sanitize_and_validation();
	test_utf8_trim_and_terminal_sanitize();
	test_utf8_case_conversions();
	test_utf8_case_idempotence();

	std::cout << "All UTF-8 unit tests passed!" << std::endl;
	return 0;
}

