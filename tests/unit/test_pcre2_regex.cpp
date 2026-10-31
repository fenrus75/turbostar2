// Tested source file: src/pcre2_regex.cpp
#include "pcre2_regex.h"
#include <cassert>
#include <iostream>
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog();

	std::cout << "Testing pcre2_regex..." << std::endl;

	// 1. Basic construction and valid check
	{
		pcre2_regex re("foo.*bar");
		assert(re.is_valid());
		assert(re.error().empty());
	}

	// 2. Invalid pattern check
	{
		pcre2_regex re("[unclosed_bracket");
		assert(!re.is_valid());
		assert(!re.error().empty());
	}

	// 3. Partial match and full match
	{
		pcre2_regex re("\\d+");
		assert(re.partial_match("abc 123 def"));
		assert(!re.full_match("abc 123 def"));
		assert(re.full_match("12345"));
		assert(!re.full_match(" 12345"));
	}

	// 4. Case-insensitivity
	{
		pcre2_regex::compile_options opts;
		opts.caseless = true;
		pcre2_regex re("hello", opts);
		assert(re.partial_match("HELLO WORLD"));
		assert(re.partial_match("HeLLo"));
	}

	// 5. find_next
	{
		pcre2_regex re("apple");
		std::string_view text = "one apple two apples";
		auto m1 = re.find_next(text, 0);
		assert(m1.has_value());
		assert(m1->byte_offset == 4);
		assert(m1->byte_length == 5);

		auto m2 = re.find_next(text, m1->byte_offset + m1->byte_length);
		assert(m2.has_value());
		assert(m2->byte_offset == 14);
		assert(m2->byte_length == 5);

		auto m3 = re.find_next(text, m2->byte_offset + m2->byte_length);
		assert(!m3.has_value());
	}

	// 6. find_all
	{
		pcre2_regex re("\\w+");
		std::string_view text = "quick brown fox";
		auto matches = re.find_all(text);
		assert(matches.size() == 3);
		assert(matches[0].byte_offset == 0 && matches[0].byte_length == 5);
		assert(matches[1].byte_offset == 6 && matches[1].byte_length == 5);
		assert(matches[2].byte_offset == 12 && matches[2].byte_length == 3);
	}

	// 7. quote_meta
	{
		std::string raw = "special [chars] (and) {more} *dots.* $^";
		std::string quoted = pcre2_regex::quote_meta(raw);
		pcre2_regex re(quoted);
		assert(re.is_valid());
		assert(re.full_match(raw));
		assert(!re.partial_match("special chars and more dots"));
	}

	// 8. global_replace
	{
		pcre2_regex re(R"(\b([a-z]+)@([a-z]+)\b)");
		std::string text = "contact foo@bar or test@example please";
		std::string replaced = re.global_replace(text, "$1 AT $2");
		assert(replaced == "contact foo AT bar or test AT example please");
	}

	// 9. Move semantics
	{
		pcre2_regex re1("test");
		assert(re1.is_valid());
		pcre2_regex re2(std::move(re1));
		assert(re2.is_valid());
		assert(re2.partial_match("a test here"));
	}

	std::cout << "All pcre2_regex unit tests passed!" << std::endl;
	return 0;
}
