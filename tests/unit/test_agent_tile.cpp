#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>
#include "test_watchdog.h"

// Access private/protected fields in unit tests to mock LLM streaming activity
#define private public
#define protected public
#include "agentlib/ai_agent.h"
#include "ui/components/ui_agent_tile.h"
#undef private
#undef protected

#include "agentlib/agent_animation.h"
#include "event_queue.h"

class mock_doc_provider : public agentlib::document_provider
{
      public:
	std::vector<std::string> get_open_document_paths() const override
	{
		return {};
	}
	std::unique_ptr<agentlib::document_snapshot> get_open_document(std::string_view) const override
	{
		return nullptr;
	}
	bool apply_live_edits(std::string_view, std::string_view) override
	{
		return false;
	}
	void save_all_documents() override
	{
	}
};

void test_agent_tile_basic()
{
	std::cout << "Testing ui_agent_tile instantiation and size..." << std::endl;
	event_queue queue;
	mock_doc_provider doc_prov;
	auto model = std::make_shared<agentlib::ai_model>("dummy", "dummy", "url", "purpose", 0.0, 0.0);
	auto agent = agentlib::ai_agent::create(1, "test_agent", model, &queue, &doc_prov);

	ui_agent_tile tile("agent_tile", 0, 0, agent);

	assert(tile.natural_width() == 22);
	assert(tile.natural_height() == 12);
	assert(tile.width() == 22);
	assert(tile.height() == 12);
}

void test_agent_tile_animation()
{
	std::cout << "Testing ui_agent_tile animation logic..." << std::endl;
	event_queue queue;
	mock_doc_provider doc_prov;
	auto model = std::make_shared<agentlib::ai_model>("dummy", "dummy", "url", "purpose", 0.0, 0.0);
	auto agent = agentlib::ai_agent::create(1, "test_agent", model, &queue, &doc_prov);

	ui_agent_tile tile("agent_tile", 0, 0, agent);

	// No token activity initially, so animation should not update
	assert(!tile.update_animation());

	// Simulate activity
	agent->update_last_activity_time();

	// The first call transitions the movie from idle to active, returning true
	assert(tile.update_animation());

	// Subsequent calls before 250ms should return false
	assert(!tile.update_animation());

	// Sleep 260ms to let quarter-second timer expire
	std::this_thread::sleep_for(std::chrono::milliseconds(260));

	// Now it should detect activity and update the animation frame!
	assert(tile.update_animation());
}

void test_agent_tile_tool_specific_animation()
{
	std::cout << "Testing ui_agent_tile tool-specific animation fallback..." << std::endl;
	event_queue queue;
	mock_doc_provider doc_prov;
	auto model = std::make_shared<agentlib::ai_model>("dummy", "dummy", "url", "purpose", 0.0, 0.0);
	auto agent = agentlib::ai_agent::create(1, "test_agent", model, &queue, &doc_prov);

	// Register a dummy animation for "fs_compile_project"
	auto &reg = agentlib::agent_animation_registry::get_instance();
	auto tool_anim = std::make_shared<agentlib::dur_animation_data>();
	tool_anim->framerate = 8.0f;
	tool_anim->size_x = 20;
	tool_anim->size_y = 8;
	// Add at least one frame
	agentlib::durmovie_frame frame;
	frame.frame_number = 1;
	frame.delay = 125;
	frame.cells.resize(8, std::vector<agentlib::durmovie_cell>(20));
	frame.cells[0][0].glyph = "C";
	tool_anim->frames.push_back(frame);

	reg.register_animation("fs_compile_project", tool_anim);

	// agent is running no tools, and animation name is "default"
	agent->set_animation_name("default");
	ui_agent_tile tile("agent_tile", 0, 0, agent);
	assert(tile.current_animation_name_ == "default");

	// Now set current tool to "fs_compile_project"
	agent->current_tool_ = "fs_compile_project";

	// Trigger update_animation - it should resolve to tool-specific animation "fs_compile_project"
	assert(tile.update_animation());
	assert(tile.current_animation_name_ == "fs_compile_project");

	// Now clear the tool name
	agent->current_tool_ = "";

	// Trigger update_animation - it should fallback back to "default"
	assert(tile.update_animation());
	assert(tile.current_animation_name_ == "default");

	// Clean up
	reg.unregister_animation("fs_compile_project");
}

int main()
{
	test_watchdog::setup_watchdog(5);
	test_agent_tile_basic();
	test_agent_tile_animation();
	test_agent_tile_tool_specific_animation();

	std::cout << "All ui_agent_tile tests passed!" << std::endl;
	return 0;
}
