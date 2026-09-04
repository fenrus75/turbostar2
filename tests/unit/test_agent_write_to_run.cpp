// Tested source file: src/tools/agent_write_to_run/agent_write_to_run_entry.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <string>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/fs_utils.h"
#include "event_queue.h"

using namespace agentlib;

class mock_doc_provider : public document_provider {
public:
	std::vector<std::string> get_open_document_paths() const override { return {}; }
	std::unique_ptr<document_snapshot> get_open_document(std::string_view) const override { return nullptr; }
	bool apply_live_edits(std::string_view, std::string_view) override { return false; }
	void save_all_documents() override {}

	bool write_to_run(int run_id, std::string_view data) override {
		if (run_id == 123) {
			last_written_data = std::string(data);
			return true;
		}
		return false;
	}

	int64_t get_run_last_modified_age(int) override {
		return 500; // Simulated age of 500ms (already settled)
	}

	void set_run_recording(int, bool recording) override {
		is_recording = recording;
	}

	std::vector<std::string> get_run_recorded_data(int) override {
		return {"simulated", " recorded", " output"};
	}

	std::string last_written_data;
	bool is_recording = false;
};

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing agent_write_to_run..." << std::endl;
	{
		mock_doc_provider mock;
		ctx.doc_provider = &mock;

		// 1. Success case: write data to valid run_id with output=false
		{
			std::string res = registry.execute_tool("agent_write_to_run", "{\"run_id\": 123, \"data\": \"echo hello\\n\", \"output\": false}", ctx);
			std::cout << "Success path result: " << res << std::endl;
			assert(res.find("Successfully wrote") != std::string::npos);
			assert(mock.last_written_data == "echo hello\n");
		}

		// 2. Execution failure case: unknown run_id
		{
			std::string res = registry.execute_tool("agent_write_to_run", "{\"run_id\": 999, \"data\": \"test\"}", ctx);
			std::cout << "Unknown run_id result: " << res << std::endl;
			assert(res.find("Error:") != std::string::npos);
		}

		// 3. Stage 1 validation failure: reject negative run_id
		{
			auto prep = registry.prepare_tool("agent_write_to_run", "{\"run_id\": -3, \"data\": \"test\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 4. Stage 1 validation failure: reject payloads exceeding size limit (4096 bytes)
		{
			std::string large_payload(4097, 'A');
			nlohmann::json args = {{"run_id", 123}, {"data", large_payload}};
			auto prep = registry.prepare_tool("agent_write_to_run", args.dump(), ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 5. Stage 1 validation failure: reject unexpected arguments
		{
			auto prep = registry.prepare_tool("agent_write_to_run", "{\"run_id\": 123, \"data\": \"test\", \"unexpected_field\": 123}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 6. Stage 2 validation failure: reject if doc_provider is null
		{
			ctx.doc_provider = nullptr;
			auto prep = registry.prepare_tool("agent_write_to_run", "{\"run_id\": 123, \"data\": \"test\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
			ctx.doc_provider = &mock;
		}

		// 7. Success case with output=true
		{
			ctx.doc_provider = &mock;
			std::string res = registry.execute_tool("agent_write_to_run", "{\"run_id\": 123, \"data\": \"test\", \"output\": true}", ctx);
			std::cout << "Success with output path result: " << res << std::endl;
			assert(fs_utils::unwrap_prompt_untrusted_data_tag(res) == "simulated recorded output");
			assert(mock.is_recording == false);
		}

	// 8. Success case with output=true and split ANSI chunks
	{
		class mock_split_ansi_provider : public mock_doc_provider {
		public:
			std::vector<std::string> get_run_recorded_data(int) override {
				// Simulate split across chunks: ESC [ in chunk 1, 31m in chunk 2
				return {"(gdb) ", "\x1b[", "31mError\x1b[0m line\n"};
			}
		};
		mock_split_ansi_provider split_mock;
		ctx.doc_provider = &split_mock;
		std::string res = registry.execute_tool("agent_write_to_run", "{\"run_id\": 123, \"data\": \"test\", \"output\": true}", ctx);
		std::cout << "Split ANSI chunks result: " << res << std::endl;
		assert(fs_utils::unwrap_prompt_untrusted_data_tag(res) == "(gdb) Error line");
	}

	std::cout << "agent_write_to_run tool verified successfully!" << std::endl;
	}

	return 0;
}
