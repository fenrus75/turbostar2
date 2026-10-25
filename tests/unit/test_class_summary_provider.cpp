// Tested source file: src/inspector/class_summary_provider.cpp
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include "codemap_utils.h"
#include "inspector/class_summary_provider.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog();

	if (!std::filesystem::exists("src/document.h") && std::filesystem::exists("../src/document.h")) {
		std::filesystem::current_path("..");
	}

	std::cout << "Testing class_summary_provider formatting across line budgets..." << std::endl;

	tools::class_summary_info sample_info;
	sample_info.class_name = "test_widget";
	sample_info.header_path = "src/ui/test_widget.h";
	sample_info.fields = {
	    {"width_", 8, 10, 10},
	    {"height_", 8, 11, 11},
	    {"title_", 8, 12, 12},
	    {"visible_", 8, 13, 13}
	};
	sample_info.methods = {
	    {"render", 6, 20, 20},
	    {"resize", 6, 21, 21},
	    {"set_visible", 6, 22, 22},
	    {"get_title", 6, 23, 23},
	    {"on_click", 6, 24, 24},
	    {"draw_border", 6, 25, 25}
	};

	// 1. Available lines = 2 (dense mode)
	{
		std::string md2 = turbostar::class_summary_provider::format_summary_markdown(sample_info, 2);
		assert(md2.find("**Fields:**") != std::string::npos);
		assert(md2.find("`width_`") != std::string::npos);
		assert(md2.find("**Methods:**") != std::string::npos);
		assert(md2.find("`render()`") != std::string::npos);
		// In 2-line mode, class header line is omitted to maximize density
		assert(md2.find("**Class:**") == std::string::npos);
	}

	// 2. Available lines = 3 (standard mode)
	{
		std::string md3 = turbostar::class_summary_provider::format_summary_markdown(sample_info, 3);
		assert(md3.find("**Class:** `test_widget`") != std::string::npos);
		assert(md3.find("4 fields, 6 methods") != std::string::npos);
		assert(md3.find("**Fields:**") != std::string::npos);
		assert(md3.find("**Methods:**") != std::string::npos);
	}

	// 3. Available lines = 4 (tall mode with overflow)
	{
		std::string md4 = turbostar::class_summary_provider::format_summary_markdown(sample_info, 4);
		assert(md4.find("**Class:** `test_widget`") != std::string::npos);
		assert(md4.find("**Fields:**") != std::string::npos);
		assert(md4.find("**Methods:**") != std::string::npos);
		assert(md4.find("**More:**") != std::string::npos);
	}

	std::cout << "Testing extract_class_summary on project files..." << std::endl;
	{
		tools::class_summary_info doc_summary;
		bool ok = tools::extract_class_summary("src/document.h", 120, 0, "document", doc_summary);
		assert(ok);
		assert(doc_summary.class_name == "document");
		assert(!doc_summary.fields.empty() || !doc_summary.methods.empty());
	}

	std::cout << "Testing class_summary_provider provider pipeline..." << std::endl;
	turbostar::class_summary_provider prov;

	// Non-C++ file returns INVALID
	{
		inspector_query q;
		q.file_path = "README.md";
		q.token = "turbostar";
		auto res = prov.query(q);
		assert(res.score == inspector_scores::INVALID);
		assert(res.is_final);
	}

	// First query on C++ header returns provisional state
	{
		inspector_query q;
		q.file_path = "src/document.h";
		q.line = 120;
		q.token = "document";
		q.available_lines = 3;

		auto res = prov.query(q);
		assert(!res.is_final);

		// Async query resolves to CLASS_TOKEN (35)
		std::mutex mtx;
		std::condition_variable cv;
		bool resolved = false;
		inspector_result async_res;

		prov.query_async(q, [&](inspector_result r) {
			std::lock_guard lock(mtx);
			async_res = r;
			resolved = true;
			cv.notify_one();
		});

		std::unique_lock lock(mtx);
		bool done = cv.wait_for(lock, std::chrono::seconds(5), [&] { return resolved; });
		assert(done);
		assert(async_res.score == inspector_scores::CLASS_SUMMARY);
		assert(async_res.title_addon.find("class document") != std::string::npos);
		assert(async_res.markdown.find("**Class:**") != std::string::npos);

		// Subsequent synchronous query hits cache immediately
		auto cached_res = prov.query(q);
		assert(cached_res.score == inspector_scores::CLASS_SUMMARY);
		assert(cached_res.is_final);
		assert(cached_res.title_addon == async_res.title_addon);
	}

	// Querying a class token from an implementation file (.cpp)
	{
		inspector_query q;
		q.file_path = "src/document.cpp";
		q.line = 408;
		q.token = "document";
		q.available_lines = 4;

		std::mutex mtx;
		std::condition_variable cv;
		bool resolved = false;
		inspector_result async_res;

		prov.query_async(q, [&](inspector_result r) {
			std::lock_guard lock(mtx);
			async_res = r;
			resolved = true;
			cv.notify_one();
		});

		std::unique_lock lock(mtx);
		bool done = cv.wait_for(lock, std::chrono::seconds(5), [&] { return resolved; });
		assert(done);
		assert(async_res.score == inspector_scores::CLASS_SUMMARY);
		assert(async_res.title_addon.find("class document") != std::string::npos);
		assert(async_res.markdown.find("**Class:** `document`") != std::string::npos);
	}

	std::cout << "All class_summary_provider unit tests passed successfully." << std::endl;
	return 0;
}
