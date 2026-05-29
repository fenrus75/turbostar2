#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include "../../src/agentlib/tool_context.h"
#include "../../src/tools/open_in_editor/open_in_editor.h"

namespace fs = std::filesystem;

int main()
{
	fs::path temp_dir = fs::temp_directory_path() / "turbostar_test_open_in_editor";
	fs::create_directories(temp_dir);

	fs::path regular_file = temp_dir / "test.txt";
	{
		std::ofstream out(regular_file);
		out << "Hello, World!\n";
		out.close();
	}

	agentlib::tool_context ctx;
	ctx.fs_security.set_working_directory(temp_dir);
	ctx.fs_security.add_allowed_root(temp_dir, agentlib::access_type::read);

	event_queue queue;
	ctx.queue = &queue;

	// Test 1: Open regular file
	{
		tools::open_in_editor_validator validator;
		nlohmann::json args = {{"filename", "test.txt"}};

		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid && "Should validate regular file path");

		auto tool = validator.create_tool(args);
		assert(tool != nullptr && "Tool should be created");

		std::string result = tool->execute(ctx);
		assert(result.find("Successfully opened") != std::string::npos && "Result should indicate success");

		// Verify event is pushed to queue
		auto ev = queue.pop();
		assert(ev.has_value() && "An event should be pushed");
		assert(ev->type == event_type::open_file && "Event type should be open_file");
		assert(!ev->payload.empty() && "Event payload should not be empty");

		std::cout << "Test 1 passed: Event correctly pushed to queue\n";
	}

	// Cleanup
	fs::remove_all(temp_dir);
	std::cout << "All open_in_editor unit tests passed!\n";
	return 0;
}
