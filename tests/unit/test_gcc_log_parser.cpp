#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>
#include "../../src/gcc_log_parser.h"

int main()
{
	gcc_log_parser parser;
	std::vector<build_error> errors;

	// Prepare a dummy file with space in path
	std::string test_dir = "test_dir_with space";
	std::filesystem::create_directories(test_dir);
	std::string test_file = test_dir + "/main.cpp";
	{
		std::ofstream ofs(test_file);
		ofs << "int main() { return 0; }\n";
	}

	// 1. Test standard parsing with space in filename
	// We convert the file path to absolute path to match the parser's absolute resolution
	std::string abs_test_file = std::filesystem::absolute(test_file).string();
	std::string line1 = abs_test_file + ":10:20: error: undefined reference";
	parser.parse_line(line1, 1, errors);

	assert(errors.size() == 1);
	assert(errors[0].filepath == abs_test_file);
	assert(errors[0].line == 9);
	assert(errors[0].column == 19);
	assert(!errors[0].is_warning);
	assert(errors[0].message == "undefined reference");

	// 2. Test warning
	std::string line2 = abs_test_file + ":5:12: warning: unused variable 'x'";
	parser.parse_line(line2, 2, errors);
	assert(errors.size() == 2);
	assert(errors[1].line == 4);
	assert(errors[1].column == 11);
	assert(errors[1].is_warning);
	assert(errors[1].message == "unused variable 'x'");

	// Clean up
	std::filesystem::remove_all(test_dir);

	std::cout << "gcc_log_parser unit test passed!\n";
	return 0;
}
