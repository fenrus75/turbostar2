// Tested source file: src/tools/run_executable/run_executable_security.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <string>
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

		std::cout << "run_executable tool verified successfully!" << std::endl;
	}

	return 0;
}
