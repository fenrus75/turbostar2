// test_markdown_extract_plugin.cpp
//
// Unit tests for the markdown_extract tool & plugin.

#include "test_watchdog.h"
#include "agentlib/ai_agent.h"
#include "agentlib/tool_context.h"
#include "agentlib/tool_registry.h"
#include "plugins/markdown_extract/markdown_extract_validator.h"
#include <cassert>
#include <iostream>
#include <string>

int main()
{
	test_watchdog::setup_watchdog(30);

	tools::markdown_extract_validator validator;

	// 1. Schema check
	assert(validator.get_name() == "markdown_extract");
	assert(validator.get_family() == "base");
	auto schema = validator.get_parameters_schema();
	assert(schema["required"].size() == 2);

	// 2. Validation failure: empty path
	{
		agentlib::tool_context ctx;
		std::string err;
		nlohmann::json invalid_args = {{"path", ""}, {"query", "ProtectKernelTunables"}};
		bool ok = validator.validate_args(invalid_args, ctx, err);
		assert(!ok);
		assert(!err.empty());
	}

	// 3. Validation success: valid path and query
	{
		agentlib::tool_context ctx;
		std::string err;
		nlohmann::json valid_args = {
			{"path", "system://man/systemd.exec.md"},
			{"query", "ProtectKernelTunables"},
			{"output_path", "tmp://extracted_tunables.md"},
			{"async", false}
		};
		bool ok = validator.validate_args(valid_args, ctx, err);
		assert(ok);
		assert(err.empty());
		auto tool = validator.create_tool(valid_args);
		assert(tool != nullptr);
	}

	// 4. Verify report_final_result is pure and executable in read_only context
	{
		agentlib::tool_context ro_ctx;
		ro_ctx.properties.read_only = true;
		auto dummy_agent = agentlib::ai_agent::create(1, "MockAgent", nullptr, nullptr, nullptr);
		ro_ctx.active_agent = dummy_agent.get();
		nlohmann::json rfr_args = {{"result", "Extracted report content"}};
		auto prep = agentlib::tool_registry::get_instance().prepare_tool("report_final_result", rfr_args.dump(), ro_ctx);
		if (prep.tool == nullptr) {
			std::cout << "Error message: " << prep.error_message << "\n";
		}
		assert(prep.tool != nullptr && "report_final_result must be executable in read_only mode");
		assert(prep.error_message.empty());
	}

	std::cout << "All test_markdown_extract_plugin tests passed successfully!\n";
	return 0;
}
