// Tested source file: src/languages/cpp/include_extractor.cpp
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "fs_utils.h"
#include "languages/cpp/include_extractor.h"
#include "test_watchdog.h"

namespace fs = std::filesystem;

int main()
{
	test_watchdog::setup_watchdog();

	std::cout << "Testing languages::cpp::include_extractor..." << std::endl;

	using languages::cpp::include_extractor;

	std::vector<std::string> lines = {
		"// #include <commented_line.h>",
		"/*",
		"#include \"commented_block.h\"",
		"*/",
		"const char *code = R\"(",
		"#include <raw_string_inc.h>",
		")\";",
		"#include <iostream> // system include",
		"#include \"my_project/header.h\"",
		"/* inline comment */ #include <vector>",
		"int main() { return 0; }"
	};

	auto incs = include_extractor::extract_includes(lines, 1);
	assert(incs.size() == 3);

	// 1. <iostream>
	assert(incs[0].path == "iostream");
	assert(incs[0].is_system == true);
	assert(incs[0].line == 8);

	// 2. "my_project/header.h"
	assert(incs[1].path == "my_project/header.h");
	assert(incs[1].is_system == false);
	assert(incs[1].line == 9);

	// 3. <vector>
	assert(incs[2].path == "vector");
	assert(incs[2].is_system == true);
	assert(incs[2].line == 10);

	// Test unique header names set
	auto names = include_extractor::extract_header_names(lines);
	assert(names.size() == 3);
	assert(names.count("iostream") == 1);
	assert(names.count("my_project/header.h") == 1);
	assert(names.count("vector") == 1);

	// Test file-based extraction
	fs::path tmp_dir = fs::path(fs_utils::get_project_tmp_dir()) / "test_cpp_inc_extractor";
	fs::create_directories(tmp_dir);
	fs::path file_p = tmp_dir / "sample.cpp";

	std::ofstream out(file_p);
	for (const auto &l : lines) {
		out << l << "\n";
	}
	out.close();

	auto file_incs = include_extractor::extract_includes_from_file(file_p.string());
	auto file_names = include_extractor::extract_header_names_from_file(file_p.string());
	fs::remove_all(tmp_dir);

	assert(file_incs.size() == 3);
	assert(file_names.size() == 3);
	assert(file_names.count("iostream") == 1);

	std::cout << "All languages::cpp::include_extractor tests passed successfully!" << std::endl;
	return 0;
}
