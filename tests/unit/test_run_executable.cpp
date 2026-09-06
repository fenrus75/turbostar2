// Tested source file: src/tools/run_executable/run_executable_security.cpp, src/tools/run_executable/run_executable_entry.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "project_manager.h"
#include "event_queue.h"

using namespace agentlib;

class dummy_doc_provider : public document_provider {
public:
	std::vector<std::string> get_open_document_paths() const override { return {}; }
	std::unique_ptr<document_snapshot> get_open_document(std::string_view) const override { return nullptr; }
	bool apply_live_edits(std::string_view, std::string_view) override { return false; }
	void save_all_documents() override {}

	start_app_result start_app(std::string_view args, bool use_debugger, bool /*auto_continue*/, bool /*collect_performance*/, std::string_view binary) override {
		last_args = std::string(args);
		last_binary = std::string(binary);
		last_debugger = use_debugger;
		return {1001, use_debugger ? 2001 : -1};
	}

	wait_for_app_result wait_for_app(int run_id, std::string_view /*type*/, int timeout_sec) override {
		last_wait_run_id = run_id;
		last_wait_timeout = timeout_sec;
		return {"ended", 150, false, ""};
	}

	void set_run_recording(int, bool recording) override {
		is_recording = recording;
		recording_history.push_back(recording);
	}

	std::vector<std::string> get_run_recorded_data(int) override {
		return {"Hello from executable\n", "Second line\n"};
	}

	std::string last_args;
	std::string last_binary;
	bool last_debugger{false};
	bool is_recording{false};
	std::vector<bool> recording_history;
	int last_wait_run_id{-1};
	int last_wait_timeout{0};
};

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;
	dummy_doc_provider dummy;
	ctx.doc_provider = &dummy;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing run_executable..." << std::endl;
	{
		// 1. Success case with default (empty) binary and normal arguments
		auto prep_default = registry.prepare_tool("run_executable", "{\"args\": \"--version\", \"debugger\": false}", ctx);
		assert(prep_default.tool != nullptr);
		assert(prep_default.error_message.empty());

		// 2. Success case with explicit binary
		auto prep_binary = registry.prepare_tool("run_executable", "{\"binary\": \"crash\", \"args\": \"--help\"}", ctx);
		assert(prep_binary.tool != nullptr);
		assert(prep_binary.error_message.empty());

		// 3. Success case with build/ prefix
		auto prep_build = registry.prepare_tool("run_executable", "{\"binary\": \"build/crash\"}", ctx);
		assert(prep_build.tool != nullptr);
		assert(prep_build.error_message.empty());

		// 4. Success case with ./build/ prefix
		auto prep_dot_build = registry.prepare_tool("run_executable", "{\"binary\": \"./build/crash\"}", ctx);
		assert(prep_dot_build.tool != nullptr);
		assert(prep_dot_build.error_message.empty());

		// 5. Success case with aliases
		auto prep_alias_exec = registry.prepare_tool("run_executable", "{\"executable\": \"crash\"}", ctx);
		assert(prep_alias_exec.tool != nullptr);
		assert(prep_alias_exec.error_message.empty());

		auto prep_alias_path = registry.prepare_tool("run_executable", "{\"path\": \"crash\"}", ctx);
		assert(prep_alias_path.tool != nullptr);
		assert(prep_alias_path.error_message.empty());

		auto prep_alias_args = registry.prepare_tool("run_executable", "{\"binary\": \"crash\", \"arguments\": \"--foo\"}", ctx);
		assert(prep_alias_args.tool != nullptr);
		assert(prep_alias_args.error_message.empty());

		// 6. Success case with debugger, collect_performance, and wait_for_time
		auto prep_options = registry.prepare_tool("run_executable", "{\"binary\": \"testcase\", \"debugger\": true, \"collect_performance\": true, \"wait_for_time\": 5}", ctx);
		assert(prep_options.tool != nullptr);
		assert(prep_options.error_message.empty());

		// 7. Security: reject binaries outside project directory
		auto prep_outside = registry.prepare_tool("run_executable", "{\"binary\": \"/bin/ls\"}", ctx);
		assert(prep_outside.tool == nullptr);
		assert(!prep_outside.error_message.empty());

		auto prep_outside2 = registry.prepare_tool("run_executable", "{\"binary\": \"/usr/bin/python3\"}", ctx);
		assert(prep_outside2.tool == nullptr);
		assert(!prep_outside2.error_message.empty());

		// 8. Security: reject path traversal in binary
		auto prep_traversal = registry.prepare_tool("run_executable", "{\"binary\": \"../../bin/ls\"}", ctx);
		assert(prep_traversal.tool == nullptr);
		assert(!prep_traversal.error_message.empty());

		// 9. Security: reject command injection in binary
		auto prep_inject_bin = registry.prepare_tool("run_executable", "{\"binary\": \"crash; rm -rf /\"}", ctx);
		assert(prep_inject_bin.tool == nullptr);
		assert(!prep_inject_bin.error_message.empty());

		// 10. Security: reject command injection / metacharacters in arguments
		auto prep_injection = registry.prepare_tool("run_executable", "{\"args\": \"; rm -rf /\"}", ctx);
		assert(prep_injection.tool == nullptr);
		assert(!prep_injection.error_message.empty());

		auto prep_injection2 = registry.prepare_tool("run_executable", "{\"args\": \"args && bad_command\"}", ctx);
		assert(prep_injection2.tool == nullptr);
		assert(!prep_injection2.error_message.empty());

		// 11. Validation: wait_for_time range
		auto prep_invalid_wait = registry.prepare_tool("run_executable", "{\"wait_for_time\": -1}", ctx);
		assert(prep_invalid_wait.tool == nullptr);
		assert(!prep_invalid_wait.error_message.empty());

		auto prep_invalid_wait2 = registry.prepare_tool("run_executable", "{\"wait_for_time\": 301}", ctx);
		assert(prep_invalid_wait2.tool == nullptr);
		assert(!prep_invalid_wait2.error_message.empty());

		// 12. Success case with output: true
		auto prep_output = registry.prepare_tool("run_executable", "{\"binary\": \"crash\", \"output\": true}", ctx);
		assert(prep_output.tool != nullptr);
		assert(prep_output.error_message.empty());

		// 13. Execution with output: true and wait_for_time: 0 (should default wait_time to 5)
		dummy.recording_history.clear();
		std::string exec_res = registry.execute_tool("run_executable", "{\"binary\": \"crash\", \"output\": true}", ctx);
		std::cout << "execute with output: true:\n" << exec_res << std::endl;
		assert(dummy.last_wait_timeout == 5);
		assert(!dummy.recording_history.empty());
		assert(dummy.recording_history.front() == true); // started recording
		assert(dummy.recording_history.back() == false); // stopped recording
		assert(exec_res.find("\"output\": \"Hello from executable\\nSecond line\\n\"") != std::string::npos);
		assert(exec_res.find("\"status\": \"ended\"") != std::string::npos);

		// 14. Execution with output: true and explicit wait_for_time: 15
		std::string exec_res2 = registry.execute_tool("run_executable", "{\"binary\": \"crash\", \"output\": true, \"wait_for_time\": 15}", ctx);
		assert(dummy.last_wait_timeout == 15);
		assert(exec_res2.find("\"output\":") != std::string::npos);

		// 15. Execution with output: false (should not include output field)
		std::string exec_res3 = registry.execute_tool("run_executable", "{\"binary\": \"crash\", \"output\": false}", ctx);
		assert(exec_res3.find("\"output\":") == std::string::npos);

		std::cout << "run_executable tool verified successfully!" << std::endl;
	}

	return 0;
}
