// Tested source file: src/agentlib/command_registry.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "agentlib/ai_agent.h"
#include "agentlib/ai_model.h"
#include "agentlib/command_registry.h"
#include "config_manager.h"
#include "event_queue.h"
#include "fs_utils.h"
#include "project_manager.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	static event_queue q;
	auto agent = ai_agent::create(1, "TestYoloAgent", model, &q, nullptr);

	std::cout << "Testing /yolo command..." << std::endl;

	auto cmd = command_registry::get_instance().get_command("yolo");
	assert(cmd != nullptr);
	assert(cmd->get_name() == "yolo");
	assert(!cmd->get_description().empty());

	agent_command::context ctx;
	ctx.agent = agent.get();
	ctx.window_id = 1;
	ctx.global_queue = &q;

	auto &cfg = config_manager::get_instance();

	// 1. Initial baseline: ensure false
	cfg.set_yolo_mode(false);
	assert(!cfg.is_yolo_mode());

	// 2. Toggle to true (empty arguments)
	ctx.arguments = "";
	cmd->execute(ctx);
	assert(cfg.is_yolo_mode());
	assert(!agent->get_interactions().empty());
	assert(agent->get_interactions().back()->get_raw_text().find("ENABLED") != std::string::npos);

	// 3. Toggle to false (empty arguments)
	ctx.arguments = "   ";
	cmd->execute(ctx);
	assert(!cfg.is_yolo_mode());
	assert(agent->get_interactions().back()->get_raw_text().find("DISABLED") != std::string::npos);

	// 4. Explicit arguments: "on", "off"
	ctx.arguments = "on";
	cmd->execute(ctx);
	assert(cfg.is_yolo_mode());
	assert(agent->get_interactions().back()->get_raw_text().find("ENABLED") != std::string::npos);

	ctx.arguments = "off";
	cmd->execute(ctx);
	assert(!cfg.is_yolo_mode());
	assert(agent->get_interactions().back()->get_raw_text().find("DISABLED") != std::string::npos);

	// 5. Numeric / boolean arguments: "1", "0", "true", "false", "ENABLE", "DISABLE"
	ctx.arguments = "1";
	cmd->execute(ctx);
	assert(cfg.is_yolo_mode());

	ctx.arguments = "0";
	cmd->execute(ctx);
	assert(!cfg.is_yolo_mode());

	ctx.arguments = "true";
	cmd->execute(ctx);
	assert(cfg.is_yolo_mode());

	ctx.arguments = "false";
	cmd->execute(ctx);
	assert(!cfg.is_yolo_mode());

	ctx.arguments = "ENABLE";
	cmd->execute(ctx);
	assert(cfg.is_yolo_mode());

	ctx.arguments = "DISABLE";
	cmd->execute(ctx);
	assert(!cfg.is_yolo_mode());

	// 6. Verify help_command lists /yolo
	auto help = command_registry::get_instance().get_command("help");
	assert(help != nullptr);
	help->execute(ctx);
	assert(!agent->get_interactions().empty());
	assert(agent->get_interactions().back()->get_raw_text().find("/yolo") != std::string::npos);

	std::cout << "All /yolo command tests passed!" << std::endl;
	return 0;
}
