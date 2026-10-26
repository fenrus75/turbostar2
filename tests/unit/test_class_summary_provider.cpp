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
	sample_info.header_lines = {
		"// line 1",
		"// line 2",
		"// line 3",
		"// line 4",
		"// line 5",
		"// line 6",
		"// line 7",
		"// line 8",
		"// line 9",
		"    int width_{0};",
		"    int height_{0};",
		"    std::string title_;",
		"    bool visible_{true};",
		"// line 14",
		"// line 15",
		"// line 16",
		"// line 17",
		"// line 18",
		"// line 19",
		"    void render();",
		"    void resize(int w, int h);",
		"    void set_visible(bool v);",
		"    std::string get_title() const;",
		"    void on_click(int x, int y);",
		"    void draw_border();"
	};
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

	// 1. Available lines = 2 (dense mode, no focused token)
	{
		std::string md2 = turbostar::class_summary_provider::format_summary_markdown(sample_info, 2);
		assert(md2.find("**Fields:**") != std::string::npos);
		assert(md2.find("`width_`") != std::string::npos);
		assert(md2.find("**Methods:**") != std::string::npos);
		assert(md2.find("`render()`") != std::string::npos);
		// Class header line is omitted to maximize density and save a line
		assert(md2.find("**Class:**") == std::string::npos);
	}

	// 2. Available lines = 3 with focused field
	{
		std::string md3 = turbostar::class_summary_provider::format_summary_markdown(sample_info, 3, "width_");
		assert(md3.find("**Class:**") == std::string::npos);
		assert(md3.find("**Fields:**") != std::string::npos);
		assert(md3.find("**Methods:**") != std::string::npos);
		assert(md3.find("**Field:**") != std::string::npos);
		assert(md3.find("`int width_{0};`") != std::string::npos);
	}

	// 3. Available lines = 3 with focused method
	{
		std::string md3m = turbostar::class_summary_provider::format_summary_markdown(sample_info, 3, "render");
		assert(md3m.find("**Class:**") == std::string::npos);
		assert(md3m.find("**Fields:**") != std::string::npos);
		assert(md3m.find("**Methods:**") != std::string::npos);
		assert(md3m.find("**Method:**") != std::string::npos);
		assert(md3m.find("`void render();`") != std::string::npos);
	}

	// 4. Available lines = 3 without focused member (shows More methods)
	{
		std::string md3 = turbostar::class_summary_provider::format_summary_markdown(sample_info, 3);
		assert(md3.find("**Class:**") == std::string::npos);
		assert(md3.find("**Fields:**") != std::string::npos);
		assert(md3.find("**Methods:**") != std::string::npos);
		assert(md3.find("**More:**") != std::string::npos);
	}

	// 5. Available lines = 4 with focused field
	{
		std::string md4 = turbostar::class_summary_provider::format_summary_markdown(sample_info, 4, "width_");
		assert(md4.find("**Class:**") == std::string::npos);
		assert(md4.find("**Fields:**") != std::string::npos);
		assert(md4.find("**Methods:**") != std::string::npos);
		assert(md4.find("**Field:**") != std::string::npos);
		assert(md4.find("`int width_{0};`") != std::string::npos);
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
		assert(async_res.markdown.find("**Class:**") == std::string::npos);
		assert(async_res.markdown.find("**Fields:**") != std::string::npos || async_res.markdown.find("**Methods:**") != std::string::npos);

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
		assert(async_res.markdown.find("**Class:**") == std::string::npos);
		assert(async_res.markdown.find("**Fields:**") != std::string::npos || async_res.markdown.find("**Methods:**") != std::string::npos);
	}

	std::cout << "All class_summary_provider unit tests passed successfully." << std::endl;
	return 0;
}
