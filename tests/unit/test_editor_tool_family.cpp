#include "test_watchdog.h"
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/agentlib/tool_validator.h"
#include "../../src/project_manager.h"
#include <cassert>
#include <iostream>
#include <memory>

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	test_watchdog::init_singletons();
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();

	auto validators = registry.get_all_registered_validators();
	auto find_val = [&](const std::string &name) -> std::shared_ptr<tool_validator> {
		for (auto &v : validators) {
			if (v->get_name() == name) return v;
		}
		return nullptr;
	};

	// Verify all 4 editor tools report get_family() == "editor"
	auto flag_err_validator = find_val("flag_as_error");
	assert(flag_err_validator != nullptr);
	assert(flag_err_validator->get_family() == "editor");

	auto clear_err_validator = find_val("clear_all_errors");
	assert(clear_err_validator != nullptr);
	assert(clear_err_validator->get_family() == "editor");

	auto status_validator = find_val("agent_set_status");
	assert(status_validator != nullptr);
	assert(status_validator->get_family() == "editor");

	auto open_ed_validator = find_val("open_in_editor");
	assert(open_ed_validator != nullptr);
	assert(open_ed_validator->get_family() == "editor");

	// Verify ask_user validator remains in "base" family
	auto ask_validator = find_val("ask_user");
	assert(ask_validator != nullptr);
	assert(ask_validator->get_family() == "base");

	// Verify auto-activation behavior
	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, nullptr, nullptr);

	// Case 1: Editor mode is false (headless/server)
	project_manager::get_instance().set_editor_mode(false);
	assert(!agent->is_tool_family_active("editor"));
	assert(!flag_err_validator->is_allowed_for_agent(agent->get_properties()));
	assert(!clear_err_validator->is_allowed_for_agent(agent->get_properties()));
	assert(!status_validator->is_allowed_for_agent(agent->get_properties()));
	assert(!open_ed_validator->is_allowed_for_agent(agent->get_properties()));
	assert(ask_validator->is_allowed_for_agent(agent->get_properties())); // ask_user still allowed!

	// Case 2: Editor mode is true (interactive editor UI)
	project_manager::get_instance().set_editor_mode(true);
	assert(agent->is_tool_family_active("editor"));
	assert(flag_err_validator->is_allowed_for_agent(agent->get_properties()));
	assert(clear_err_validator->is_allowed_for_agent(agent->get_properties()));
	assert(status_validator->is_allowed_for_agent(agent->get_properties()));
	assert(open_ed_validator->is_allowed_for_agent(agent->get_properties()));
	assert(ask_validator->is_allowed_for_agent(agent->get_properties()));

	// Reset editor mode to false
	project_manager::get_instance().set_editor_mode(false);

	std::cout << "Editor tool family auto-activation unit tests passed successfully!\n";
	return 0;
}
