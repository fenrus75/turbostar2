// Tested source file: src/inspector/man_page_provider.cpp
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include "inspector/man_page_provider.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog();

	std::cout << "Testing prototype collapsing..." << std::endl;
	std::string multiline =
	    "int execve(const char *pathname,\n"
	    "           char *const argv[],\n"
	    "           char *const envp[]);";
	std::string collapsed = man_page_provider::collapse_prototype(multiline);
	assert(collapsed == "int execve(const char *pathname, char *const argv[], char *const envp[]);");

	std::string spaced = "void   * malloc ( size_t   size ) ;";
	std::string cleaned_spaced = man_page_provider::collapse_prototype(spaced);
	assert(cleaned_spaced == "void * malloc(size_t size);");

	std::cout << "Testing synopsis prototype extraction..." << std::endl;
	std::string sample_md =
	    "# NAME\n"
	    "printf, fprintf, sprintf, snprintf - formatted output conversion\n\n"
	    "# SYNOPSIS\n"
	    "#include <stdio.h>\n\n"
	    "int printf(const char *restrict format, ...);\n"
	    "int fprintf(FILE *restrict stream,\n"
	    "            const char *restrict format, ...);\n"
	    "int sprintf(char *restrict str,\n"
	    "            const char *restrict format, ...);\n"
	    "int snprintf(char *restrict str, size_t size,\n"
	    "             const char *restrict format, ...);\n\n"
	    "# DESCRIPTION\n"
	    "The functions in the printf() family produce output according to a format.\n";

	std::string proto_snprintf = man_page_provider::extract_synopsis_prototype(sample_md, "snprintf");
	assert(!proto_snprintf.empty());
	assert(proto_snprintf.find("snprintf") != std::string::npos);
	assert(proto_snprintf.find("size_t size") != std::string::npos);
	assert(proto_snprintf.back() == ';');

	std::string proto_fprintf = man_page_provider::extract_synopsis_prototype(sample_md, "fprintf");
	assert(!proto_fprintf.empty());
	assert(proto_fprintf.find("fprintf") != std::string::npos);
	assert(proto_fprintf.find("FILE *restrict stream") != std::string::npos);

	std::cout << "Testing balanced parenthesis counter and nested function pointers..." << std::endl;
	std::string qsort_md =
	    "# SYNOPSIS\n"
	    "#include <stdlib.h>\n\n"
	    "void qsort(void *base, size_t nmemb, size_t size,\n"
	    "           int (*compar)(const void *, const void *));\n";
	std::string proto_qsort = man_page_provider::extract_synopsis_prototype(qsort_md, "qsort");
	assert(proto_qsort == "void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));");

	std::string comment_semi_md =
	    "# SYNOPSIS\n"
	    "int execl(const char *path, const char *arg, ... /*, (char *) NULL; */);\n";
	std::string proto_comment_semi = man_page_provider::extract_synopsis_prototype(comment_semi_md, "execl");
	assert(proto_comment_semi.find("NULL;") != std::string::npos);
	assert(proto_comment_semi.back() == ';');

	std::cout << "Testing short description extraction..." << std::endl;
	std::string desc = man_page_provider::extract_short_description(sample_md, "printf");
	assert(desc == "Formatted output conversion.");

	std::cout << "Testing live man page provider resolution and caching..." << std::endl;
	man_page_provider provider("/usr/share/man/man3");
	assert(provider.name() == "man");

	// Negative lookup test
	inspector_query neg_q;
	neg_q.token = "completely_fictitious_symbol_12345";
	auto neg_res = provider.query(neg_q);
	assert(neg_res.score == inspector_scores::INVALID);
	assert(neg_res.is_final == true);
	assert(provider.cache_size() == 1); // Negative entry cached

	// Second query should be an instant negative cache hit
	auto neg_res2 = provider.query(neg_q);
	assert(neg_res2.score == inspector_scores::INVALID);

	// Test real symbol if man pages are installed
	inspector_query pos_q;
	pos_q.token = "printf";
	auto sync_res = provider.query(pos_q);

	if (sync_res.is_final) {
		// Was resolved immediately or not found
		if (sync_res.score > 0) {
			assert(sync_res.score == inspector_scores::MAN_PAGE);
			assert(sync_res.markdown.find("printf") != std::string::npos);
		}
	} else {
		// Provisional response: test async query resolution
		assert(sync_res.score == inspector_scores::RESOLVING_HINT);

		std::mutex cv_m;
		std::condition_variable cv;
		bool done = false;
		inspector_result async_res;

		provider.query_async(pos_q, [&](inspector_result res) {
			std::lock_guard<std::mutex> lk(cv_m);
			done = true;
			async_res = res;
			cv.notify_one();
		});

		{
			std::unique_lock<std::mutex> lk(cv_m);
			cv.wait_for(lk, std::chrono::seconds(3), [&]() { return done; });
		}

		assert(done);
		assert(async_res.score == inspector_scores::MAN_PAGE);
		assert(async_res.markdown.find("printf") != std::string::npos);
		assert(async_res.markdown.find("Signature:") != std::string::npos);

		// Now query again: must be an instant synchronous warm cache hit!
		auto warm_res = provider.query(pos_q);
		assert(warm_res.score == inspector_scores::MAN_PAGE);
		assert(warm_res.is_final == true);
		assert(warm_res.markdown.find("printf") != std::string::npos);
	}

	// Test execv (which is in a shared multi-function man page exec.3 with variable declarations)
	inspector_query execv_q;
	execv_q.token = "execv";
	std::mutex cv_execv_m;
	std::condition_variable cv_execv;
	bool execv_done = false;
	inspector_result execv_res;
	provider.query_async(execv_q, [&](inspector_result res) {
		std::lock_guard<std::mutex> lk(cv_execv_m);
		execv_done = true;
		execv_res = res;
		cv_execv.notify_one();
	});
	{
		std::unique_lock<std::mutex> lk(cv_execv_m);
		cv_execv.wait_for(lk, std::chrono::seconds(2), [&]() { return execv_done; });
	}
	assert(execv_done);
	assert(execv_res.score == inspector_scores::MAN_PAGE);
	assert(execv_res.markdown.find("int execv(const char *path, char *const argv[]);") != std::string::npos);
	assert(execv_res.markdown.find("Execute a file.") != std::string::npos);

	std::cout << "Testing cache clearing..." << std::endl;
	provider.clear_cache();
	assert(provider.cache_size() == 0);

	std::cout << "man_page_provider tests passed successfully!" << std::endl;
	return 0;
}
