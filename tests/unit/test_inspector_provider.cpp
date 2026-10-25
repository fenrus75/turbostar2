// Tested source file: src/inspector/inspector_coordinator.cpp
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include "inspector/fallback_provider.h"
#include "inspector/inspector_coordinator.h"
#include "inspector/inspector_helpers.h"
#include "inspector/lsp_provider.h"
#include "test_watchdog.h"

class mock_diagnostic_provider : public inspector_provider
{
      public:
	std::string_view name() const noexcept override { return "linter"; }
	inspector_result query(const inspector_query &q) override
	{
		inspector_result res;
		res.provider_name = "linter";
		res.is_final = true;
		if (q.token == "deprecated_func") {
			res.score = 95;
			res.markdown = "**Warning:** `deprecated_func` is deprecated";
		} else {
			res.score = 0;
			res.markdown = "";
		}
		return res;
	}
};

class mock_async_provider : public inspector_provider
{
      public:
	std::string_view name() const noexcept override { return "ai"; }
	inspector_result query(const inspector_query &q) override
	{
		inspector_result res;
		res.provider_name = "ai";
		res.score = 25;
		res.markdown = std::format("`{}` *[analyzing...]*", q.token);
		res.is_final = false;
		return res;
	}

	void query_async(const inspector_query &q, std::function<void(inspector_result)> callback) override
	{
		std::thread([q, callback]() {
			inspector_result res;
			res.provider_name = "ai";
			res.score = 88;
			res.markdown = std::format("**AI Summary:** Context for `{}`", q.token);
			res.is_final = true;
			if (callback) {
				callback(res);
			}
		}).detach();
	}
};

int main()
{
	test_watchdog::setup_watchdog();

	std::cout << "Testing markdown_builder helpers..." << std::endl;
	inspector::markdown_builder mb;
	mb.header("Signature:").code("void process()").newline();
	mb.header("Args:").numbered_arg(1, "param1").text("  ").numbered_arg(2, "param2").newline();
	mb.header("Decl:").link("src/core.h:10", "file:///src/core.h");
	std::string built = mb.build();
	assert(built.find("**Signature:** `void process()`") != std::string::npos);
	assert(built.find("1. `param1`") != std::string::npos);
	assert(built.find("2. `param2`") != std::string::npos);
	assert(built.find("[src/core.h:10](file:///src/core.h)") != std::string::npos);

	std::cout << "Testing fallback_inspector_provider..." << std::endl;
	fallback_inspector_provider fallback;
	assert(fallback.name() == "token");

	inspector_query empty_q;
	auto res_empty = fallback.query(empty_q);
	assert(res_empty.score == 0);
	assert(res_empty.markdown == "*[No symbol under cursor]*");
	assert(res_empty.is_final == true);

	inspector_query token_q;
	token_q.token = "my_var";
	auto res_token = fallback.query(token_q);
	assert(res_token.score == 15);
	assert(res_token.markdown == "`my_var`");
	assert(res_token.is_final == true);

	std::cout << "Testing lsp_inspector_provider..." << std::endl;
	lsp_inspector_provider lsp_prov(nullptr);
	assert(lsp_prov.name() == "lsp");
	auto lsp_res_empty = lsp_prov.query(empty_q);
	assert(lsp_res_empty.score == 0);

	auto lsp_res_pending = lsp_prov.query(token_q);
	assert(lsp_res_pending.score == 10);
	assert(lsp_res_pending.is_final == false);
	assert(lsp_res_pending.markdown.find("resolving") != std::string::npos);

	std::cout << "Testing inspector_coordinator with multiple providers and ranking..." << std::endl;
	inspector_coordinator coordinator;
	coordinator.register_provider(std::make_unique<fallback_inspector_provider>());
	coordinator.register_provider(std::make_unique<mock_diagnostic_provider>());

	// For a regular token, fallback (score 15) should win over linter (score 0)
	inspector_query normal_q;
	normal_q.token = "process_data";
	normal_q.request_id = coordinator.next_request_id();
	auto best_normal = coordinator.query(normal_q, nullptr);
	assert(best_normal.provider_name == "token");
	assert(best_normal.score == 15);
	assert(best_normal.markdown == "`process_data`");

	// Test score 0 is INVALID and never selected even if it is the only provider
	inspector_coordinator invalid_test_coordinator;
	invalid_test_coordinator.register_provider(std::make_unique<mock_diagnostic_provider>());
	auto best_invalid = invalid_test_coordinator.query(normal_q, nullptr);
	assert(best_invalid.score == inspector_scores::INVALID);
	assert(best_invalid.provider_name == "none");
	assert(best_invalid.markdown == "*[No symbol under cursor]*");

	// For "deprecated_func", linter (score 95) must beat fallback (score 15)
	inspector_query warning_q;
	warning_q.token = "deprecated_func";
	warning_q.request_id = coordinator.next_request_id();
	auto best_warning = coordinator.query(warning_q, nullptr);
	assert(best_warning.provider_name == "linter");
	assert(best_warning.score == 95);
	assert(best_warning.markdown.find("Warning") != std::string::npos);

	std::cout << "Testing asynchronous provider resolution in coordinator..." << std::endl;
	inspector_coordinator async_coordinator;
	async_coordinator.register_provider(std::make_unique<fallback_inspector_provider>());
	async_coordinator.register_provider(std::make_unique<mock_async_provider>());

	std::mutex cv_m;
	std::condition_variable cv;
	bool async_received = false;
	inspector_result async_out;

	inspector_query async_q;
	async_q.token = "vector_buffer";
	async_q.request_id = async_coordinator.next_request_id();

	auto sync_res = async_coordinator.query(async_q, [&](inspector_result res) {
		std::lock_guard<std::mutex> lk(cv_m);
		async_received = true;
		async_out = res;
		cv.notify_one();
	});

	// Synchronously, mock_async returns 25 which beats fallback's 15
	assert(sync_res.provider_name == "ai");
	assert(sync_res.score == 25);
	assert(sync_res.is_final == false);

	// Wait for async promotion
	{
		std::unique_lock<std::mutex> lk(cv_m);
		cv.wait_for(lk, std::chrono::seconds(2), [&]() { return async_received; });
	}

	assert(async_received);
	assert(async_out.provider_name == "ai");
	assert(async_out.score == 88);
	assert(async_out.is_final == true);
	assert(async_out.markdown.find("AI Summary") != std::string::npos);

	std::cout << "inspector_provider tests passed successfully!" << std::endl;
	return 0;
}
