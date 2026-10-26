// Tested source file: src/inspector/enclosing_class_provider.cpp
#include <cassert>
#include <filesystem>
#include <iostream>
#include "inspector/enclosing_class_provider.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog();

	if (!std::filesystem::exists("src/document.cpp") && std::filesystem::exists("../src/document.cpp")) {
		std::filesystem::current_path("..");
	}

	std::cout << "Testing enclosing_class_provider..." << std::endl;
	turbostar::enclosing_class_provider prov;

	// Non-C++ file returns INVALID
	{
		inspector_query q;
		q.file_path = "README.md";
		q.line = 1;
		auto res = prov.query(q);
		assert(res.score == inspector_scores::INVALID);
		assert(res.is_final);
	}

	// Inside document.cpp member function (e.g. line 408 document::restore_cursor_state)
	{
		inspector_query q;
		q.file_path = "src/document.cpp";
		q.line = 408;
		q.available_lines = 3;

		auto res = prov.query(q);
		assert(res.score == inspector_scores::CLASS_ENCLOSING);
		assert(res.score == 34);
		assert(res.is_final);
		assert(res.provider_name == "enclosing_class");
		assert(res.title_addon.find("class document") != std::string::npos);
		assert(res.markdown.find("**Class:**") == std::string::npos);
		assert(res.markdown.find("**Fields:**") != std::string::npos || res.markdown.find("**Methods:**") != std::string::npos);

		// Cache hit
		auto res2 = prov.query(q);
		assert(res2.score == 34);
		assert(res2.title_addon == res.title_addon);

		// With focused field under cursor (e.g. lines_)
		q.token = "lines_";
		auto res_field = prov.query(q);
		assert(res_field.score == 34);
		assert(res_field.markdown.find("**Field:**") != std::string::npos);
		assert(res_field.markdown.find("`std::vector<std::shared_ptr<line>> lines_;`") != std::string::npos);

		// With focused method under cursor (e.g. get_lines_at_undo)
		q.token = "get_lines_at_undo";
		auto res_method = prov.query(q);
		assert(res_method.score == 34);
		assert(res_method.markdown.find("**Method:**") != std::string::npos);
		assert(res_method.markdown.find("get_lines_at_undo") != std::string::npos);
	}

	// Inside document.h class definition (e.g. line 120)
	{
		inspector_query q;
		q.file_path = "src/document.h";
		q.line = 120;
		q.available_lines = 4;

		auto res = prov.query(q);
		assert(res.score == inspector_scores::CLASS_ENCLOSING);
		assert(res.title_addon.find("class document") != std::string::npos);
		assert(res.markdown.find("**Class:**") == std::string::npos);
		assert(res.markdown.find("**Fields:**") != std::string::npos || res.markdown.find("**Methods:**") != std::string::npos);
	}

	std::cout << "All enclosing_class_provider unit tests passed successfully." << std::endl;
	return 0;
}
