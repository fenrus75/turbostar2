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
#undef private
#undef protected

#include "event_queue.h"
#include "ui/components/ui_agent_tile.h"

class mock_doc_provider : public agentlib::document_provider
{
      public:
	std::vector<std::string> get_open_document_paths() const override
	{
		return {};
	}
	std::unique_ptr<agentlib::document_snapshot> get_open_document(const std::string &) const override
	{
		return nullptr;
	}
	bool apply_live_edits(const std::string &, const std::string &) override
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

	assert(tile.natural_width() == 20);
	assert(tile.natural_height() == 10);
	assert(tile.width() == 20);
	assert(tile.height() == 10);
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

	// Simulate token activity
	agent->tokens_rx_ = 10;

	// Elapsed time hasn't passed 250ms yet, so it shouldn't update
	assert(!tile.update_animation());

	// Sleep 260ms to let quarter-second timer expire
	std::this_thread::sleep_for(std::chrono::milliseconds(260));

	// Now it should detect token increase and update the animation!
	assert(tile.update_animation());
}

int main()
{
	test_watchdog::setup_watchdog(5);
	test_agent_tile_basic();
	test_agent_tile_animation();

	std::cout << "All ui_agent_tile tests passed!" << std::endl;
	return 0;
}
