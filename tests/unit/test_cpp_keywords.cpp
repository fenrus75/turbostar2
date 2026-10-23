// Tested source file: src/languages/cpp/keywords.cpp
#include <cassert>
#include <iostream>
#include <string_view>
#include "languages/cpp/keywords.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog();

	std::cout << "Testing languages::cpp::keywords..." << std::endl;

	using languages::cpp::keywords;

	// 1. is_keyword
	assert(keywords::is_keyword("if"));
	assert(keywords::is_keyword("return"));
	assert(keywords::is_keyword("constexpr"));
	assert(keywords::is_keyword("consteval"));
	assert(keywords::is_keyword("constinit"));
	assert(keywords::is_keyword("co_await"));
	assert(keywords::is_keyword("concept"));
	assert(keywords::is_keyword("requires"));
	assert(keywords::is_keyword("alignas"));
	assert(keywords::is_keyword("alignof"));
	assert(!keywords::is_keyword("my_var"));
	assert(!keywords::is_keyword("CustomClass"));

	// 2. is_primitive_type
	assert(keywords::is_primitive_type("int"));
	assert(keywords::is_primitive_type("void"));
	assert(keywords::is_primitive_type("char32_t"));
	assert(keywords::is_primitive_type("size_t"));
	assert(keywords::is_primitive_type("ptrdiff_t"));
	assert(keywords::is_primitive_type("uintptr_t"));
	assert(keywords::is_primitive_type("u32"));
	assert(!keywords::is_primitive_type("Widget"));
	assert(!keywords::is_primitive_type("string"));

	// 3. is_function_specifier
	assert(keywords::is_function_specifier("static"));
	assert(keywords::is_function_specifier("inline"));
	assert(keywords::is_function_specifier("virtual"));
	assert(keywords::is_function_specifier("constexpr"));
	assert(keywords::is_function_specifier("constinit"));
	assert(keywords::is_function_specifier("void"));
	assert(keywords::is_function_specifier("ptrdiff_t"));
	assert(keywords::is_function_specifier("char32_t"));
	assert(keywords::is_function_specifier("__always_inline"));
	assert(!keywords::is_function_specifier("MyFunction"));
	assert(!keywords::is_function_specifier("while"));

	// 4. is_std_type_or_container
	assert(keywords::is_std_type_or_container("vector"));
	assert(keywords::is_std_type_or_container("string"));
	assert(keywords::is_std_type_or_container("string_view"));
	assert(keywords::is_std_type_or_container("optional"));
	assert(keywords::is_std_type_or_container("span"));
	assert(keywords::is_std_type_or_container("jthread"));
	assert(!keywords::is_std_type_or_container("MyVector"));

	// 5. is_call_candidate_denylisted
	assert(keywords::is_call_candidate_denylisted("if"));
	assert(keywords::is_call_candidate_denylisted("co_await"));
	assert(keywords::is_call_candidate_denylisted("sizeof"));
	assert(keywords::is_call_candidate_denylisted("alignas"));
	assert(keywords::is_call_candidate_denylisted("static_cast"));
	assert(keywords::is_call_candidate_denylisted("likely"));
	assert(!keywords::is_call_candidate_denylisted("execute_task"));
	assert(!keywords::is_call_candidate_denylisted("process_item"));

	// 6. is_type_candidate_denylisted
	assert(keywords::is_type_candidate_denylisted("int"));
	assert(keywords::is_type_candidate_denylisted("ptrdiff_t"));
	assert(keywords::is_type_candidate_denylisted("uintptr_t"));
	assert(keywords::is_type_candidate_denylisted("constinit"));
	assert(keywords::is_type_candidate_denylisted("vector"));
	assert(keywords::is_type_candidate_denylisted("string"));
	assert(keywords::is_type_candidate_denylisted("SOME_MACRO"));
	assert(!keywords::is_type_candidate_denylisted("Widget"));
	assert(!keywords::is_type_candidate_denylisted("AppContext"));

	std::cout << "All languages::cpp::keywords tests passed successfully!" << std::endl;
	return 0;
}
