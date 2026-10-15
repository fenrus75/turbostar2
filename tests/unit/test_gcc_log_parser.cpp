// Tested source file: src/gcc_log_parser.cpp
#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "config_manager.h"
#include "gcc_log_parser.h"

namespace
{

struct temp_test_env
{
	std::filesystem::path dir;
	std::filesystem::path main_cpp;

	temp_test_env()
	{
		dir = std::filesystem::temp_directory_path() / "test_gcc_log_parser_env";
		std::filesystem::create_directories(dir);
		main_cpp = dir / "main.cpp";
		std::ofstream ofs(main_cpp);
		ofs << "int main() { return 0; }\n";
	}

	~temp_test_env()
	{
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
	}
};

} // namespace

void test_error_severities(const temp_test_env &env)
{
	gcc_log_parser parser;
	std::vector<build_error> errors;
	std::string abs_path = std::filesystem::absolute(env.main_cpp).string();

	// 1. Standard error
	std::string err_line = abs_path + ":10:20: error: undefined reference to 'foo()'";
	parser.parse_line(err_line, 1, errors);
	assert(errors.size() == 1);
	assert(errors[0].filepath == abs_path);
	assert(errors[0].line == 9);
	assert(errors[0].column == 19);
	assert(errors[0].end_column == 0);
	assert(!errors[0].is_warning);
	assert(errors[0].message == "undefined reference to 'foo()'");
	assert(errors[0].output_buffer_line == 1);

	// 2. Warning
	std::string warn_line = abs_path + ":5:12: warning: unused variable 'x' [-Wunused-variable]";
	parser.parse_line(warn_line, 2, errors);
	assert(errors.size() == 2);
	assert(errors[1].filepath == abs_path);
	assert(errors[1].line == 4);
	assert(errors[1].column == 11);
	assert(errors[1].end_column == 0);
	assert(errors[1].is_warning);
	assert(errors[1].message == "unused variable 'x' [-Wunused-variable]");
	assert(errors[1].output_buffer_line == 2);

	// 3. Fatal error
	std::string fatal_line = abs_path + ":1:10: fatal error: missing_header.h: No such file or directory";
	parser.parse_line(fatal_line, 3, errors);
	assert(errors.size() == 3);
	assert(errors[2].filepath == abs_path);
	assert(errors[2].line == 0);
	assert(errors[2].column == 9);
	assert(errors[2].end_column == 0);
	assert(!errors[2].is_warning);
	assert(errors[2].message == "missing_header.h: No such file or directory");
	assert(errors[2].output_buffer_line == 3);
}

void test_file_path_handling()
{
	gcc_log_parser parser;
	std::vector<build_error> errors;

	// 1. Path with spaces in directory name
	std::filesystem::path space_dir = std::filesystem::temp_directory_path() / "test dir with spaces";
	std::filesystem::create_directories(space_dir);
	std::filesystem::path space_file = space_dir / "space source.cpp";
	{
		std::ofstream ofs(space_file);
		ofs << "int x = 1;\n";
	}

	std::string space_path = std::filesystem::absolute(space_file).string();
	std::string space_line = space_path + ":42:15: error: expected ';' after expression";
	parser.parse_line(space_line, 10, errors);
	assert(errors.size() == 1);
	assert(errors[0].filepath == space_path);
	assert(errors[0].line == 41);
	assert(errors[0].column == 14);

	// 2. Relative path resolution against config_manager build directory
	std::string orig_build_dir = config_manager::get_instance().get_build_directory();
	config_manager::get_instance().set_build_directory(space_dir.string());

	// "space source.cpp" is relative, should resolve against space_dir
	std::string rel_line = "space source.cpp:2:3: warning: implicit conversion";
	parser.parse_line(rel_line, 20, errors);
	assert(errors.size() == 2);
	assert(errors[1].filepath == space_path);
	assert(errors[1].line == 1);
	assert(errors[1].column == 2);
	assert(errors[1].is_warning);

	// Restore original build directory
	config_manager::get_instance().set_build_directory(orig_build_dir);

	// 3. Nonexistent file path (must be rejected because file does not exist on disk)
	std::string non_existent_line = "/nonexistent/path/does_not_exist.cpp:1:1: error: boom";
	parser.parse_line(non_existent_line, 30, errors);
	assert(errors.size() == 2); // No new error added

	// Cleanup
	std::error_code ec;
	std::filesystem::remove_all(space_dir, ec);
}

void test_complex_messages(const temp_test_env &env)
{
	gcc_log_parser parser;
	std::vector<build_error> errors;
	std::string abs_path = std::filesystem::absolute(env.main_cpp).string();

	// 1. Message containing colons
	std::string line_colons = abs_path + ":12:5: error: expected ':' after 'case'";
	parser.parse_line(line_colons, 1, errors);
	assert(errors.size() == 1);
	assert(errors[0].message == "expected ':' after 'case'");

	// 2. Message containing quotes, brackets, and templates
	std::string line_tmpl = abs_path + ":88:14: error: no matching member function for call to 'push_back'";
	parser.parse_line(line_tmpl, 2, errors);
	assert(errors.size() == 2);
	assert(errors[1].message == "no matching member function for call to 'push_back'");

	// 3. Message containing paths
	std::string line_path_msg = abs_path + ":3:1: fatal error: /usr/include/math.h: file too short";
	parser.parse_line(line_path_msg, 3, errors);
	assert(errors.size() == 3);
	assert(!errors[2].is_warning);
	assert(errors[2].message == "/usr/include/math.h: file too short");
}

void test_coordinate_edge_cases(const temp_test_env &env)
{
	gcc_log_parser parser;
	std::vector<build_error> errors;
	std::string abs_path = std::filesystem::absolute(env.main_cpp).string();

	// Minimum 1-based coordinates
	std::string min_line = abs_path + ":1:1: error: first char error";
	parser.parse_line(min_line, 0, errors);
	assert(errors.size() == 1);
	assert(errors[0].line == 0);
	assert(errors[0].column == 0);
	assert(errors[0].output_buffer_line == 0);

	// Large line and column numbers
	std::string large_line = abs_path + ":1000000:500: warning: large coordinates";
	parser.parse_line(large_line, 99, errors);
	assert(errors.size() == 2);
	assert(errors[1].line == 999999);
	assert(errors[1].column == 499);
	assert(errors[1].output_buffer_line == 99);
}

void test_non_matching_lines(const temp_test_env &env)
{
	gcc_log_parser parser;
	std::vector<build_error> errors;
	std::string abs_path = std::filesystem::absolute(env.main_cpp).string();

	// Compiler progress and info lines
	parser.parse_line("[ 50%] Building CXX object CMakeFiles/app.dir/main.cpp.o", 1, errors);
	parser.parse_line("ninja: Entering directory `build'", 2, errors);
	parser.parse_line("g++ -Wall -Wextra -O3 -c main.cpp", 3, errors);

	// Note and remark lines (should not be recorded as primary errors/warnings)
	parser.parse_line(abs_path + ":10:5: note: candidate function not viable", 4, errors);
	parser.parse_line(abs_path + ":10:5: remark: loop vectorized", 5, errors);

	// Missing column number (old GCC format without column)
	parser.parse_line(abs_path + ":10: error: message without column", 6, errors);

	// Lines with random error keywords
	parser.parse_line("Scanning dependencies of target error_manager", 7, errors);
	parser.parse_line("fatal: not a git repository", 8, errors);

	// Empty and whitespace lines
	parser.parse_line("", 9, errors);
	parser.parse_line("    ", 10, errors);

	assert(errors.empty());
}

int main()
{
	test_watchdog::setup_watchdog(30);

	temp_test_env env;
	test_error_severities(env);
	test_file_path_handling();
	test_complex_messages(env);
	test_coordinate_edge_cases(env);
	test_non_matching_lines(env);

	std::cout << "All gcc_log_parser unit tests passed successfully!\n";
	return 0;
}
