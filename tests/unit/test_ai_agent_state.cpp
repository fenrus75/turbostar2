// =============================================================================
// test_ai_agent_state.cpp
//
// Unit tests for the state / accessor / admin surface of agentlib::ai_agent.
//
// SOURCE FILE COVERED:
//   src/agentlib/ai_agent.cpp
//
// METHODS COVERED:
//   - agent_status_to_string(status, tool_name), agent_status_to_name(status)
//   - create(), get_id(), get_name(), get_status(), get_global_queue()
//   - set_status(s), set_status(waiting, target_id), get_waiting_on_id()
//   - set_model(), get_model()
//   - Token/cost accounting getters: get_tokens_tx(), get_tokens_rx(),
//     get_tokens_cached(), get_active_tokens(), get_estimated_cost(),
//     get_last_boundary_prob(), get_last_inference_duration_ms()
//   - increment_stat(key), increment_stat(key, amount), get_stats()
//   - get_interactions(), add_interaction()
//   - is_read_only(), set_read_only(), get_role(), set_role(),
//     get_properties(), set_properties()
//   - get_allowed_write_file(), set_allowed_write_file(), is_mutation_possible()
//   - get_task_description(), set_task_description()
//   - set_exit_implicitly_on_idle(), is_exit_implicitly_on_idle()
//   - set_notify_parent_on_completion(), is_notify_parent_on_completion()
//   - set_suppress_parent_injection(), is_suppress_parent_injection()
//   - get_animation_name(), set_animation_name()
//   - update_last_activity_time(), get_last_activity_time_ms()
//   - get_current_tool()
//   - get_all_active_agents(), find_agent_by_id()
//   - get_conversation(), get_conversation_data(), get_current_system_prompt()
//   - get_compaction_segments(), clear_conversation()
//   - spawn_subagent(), get_subagents(), remove_subagent(), get_parent()
//
// NOTE ON COVERAGE SCOPE:
//   The ~30 pre-existing ai_agent tests only exercise create / inject_context /
//   paging / conversation flows. This file targets the cheap getter/setter and
//   simple admin methods, which are otherwise 100% uncovered.
//
// NOTE ON GLOBAL REGISTRY:
//   ai_agent::create() registers the agent in a process-wide registry
//   (g_agent_registry in ai_agent.cpp). Every agent created here is closed()
//   and dropped at the end of each scenario so later tests observe a clean
//   registry (get_all_active_agents() must not see stale agents).
// =============================================================================

#include "test_watchdog.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/interactions/interactions.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/config_manager.h"
#include "../../src/event_logger.h"
#include "../../src/event_queue.h"
#include "../../src/project_manager.h"

using namespace agentlib;

static event_queue g_queue;

// Creates a fresh agent with a unique id / name and the shared test model.
// We DO NOT trigger any processing (no submit_prompt/inject trigger), so the
// model at "http://localhost:1" is never contacted.
static std::shared_ptr<ai_agent> make_agent(int id, const std::string &name)
{
	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost:1", "Test", 0.0, 0.0);
	ai_model_registry::get_instance().register_model(model);
	config_manager::get_instance().set_default_model_id("test-model");
	return ai_agent::create(id, name, model, &g_queue, nullptr);
}

static void print_path(const std::string &test_name, bool ok)
{
	std::cout << (ok ? "PASS: " : "FAIL: ") << test_name << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	// The agent writes active-state JSON under ~/.cache/turbostar; isolate HOME
	// so parallel test runs do not clobber each other or the user's config.
	test_watchdog::isolate_home("ai_agent_state");
	event_logger::get_instance().enable_stdout_logging(true);
	project_manager::get_instance().initialize();

	// ------------------------------------------------------------------ 1.
	// agent_status_to_string / agent_status_to_name for every enum value.
	// ------------------------------------------------------------------ 1.
	{
		std::string t = "agent_status_to_string/name";
		assert(agent_status_to_string(agent_status::idle) == "Idle");
		assert(agent_status_to_string(agent_status::thinking) == "Thinking...");
		assert(agent_status_to_string(agent_status::tool_execution) == "Running Tool...");
		assert(agent_status_to_string(agent_status::waiting) == "Waiting...");
		assert(agent_status_to_string(agent_status::error) == "Error");
		assert(agent_status_to_string(agent_status::dead) == "Dead");
		// tool name is appended in the tool_execution case
		assert(agent_status_to_string(agent_status::idle, "my_tool") == "Idle");
		assert(agent_status_to_string(agent_status::tool_execution, "my_tool") == "Running Tool: my_tool");

		assert(agent_status_to_name(agent_status::idle) == "Idle");
		assert(agent_status_to_name(agent_status::thinking) == "Thinking");
		assert(agent_status_to_name(agent_status::tool_execution) == "Tool Execution");
		assert(agent_status_to_name(agent_status::waiting) == "Waiting");
		assert(agent_status_to_name(agent_status::error) == "Error");
		assert(agent_status_to_name(agent_status::dead) == "Dead");
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 2.
	// create() basic accessors: id, name, initial status idle, queue.
	// ------------------------------------------------------------------ 2.
	{
		std::string t = "create getters";
		int id = 5001;
		auto agent = make_agent(id, "AdminAgent");
		assert(agent->get_id() == id);
		assert(agent->get_name() == "AdminAgent");
		assert(agent->get_status() == agent_status::idle);
		assert(agent->get_global_queue() == &g_queue);
		agent->close();
		agent.reset();
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 3.
	// set_status/get_status transitions + waiting target id.
	// ------------------------------------------------------------------ 3.
	{
		std::string t = "set_status transitions";
		auto agent = make_agent(5002, "StatusAgent");

		agent->set_status(agent_status::thinking);
		assert(agent->get_status() == agent_status::thinking);
		assert(agent->get_waiting_on_id() == -1);

		agent->set_status(agent_status::tool_execution);
		assert(agent->get_status() == agent_status::tool_execution);

		agent->set_status(agent_status::waiting, 42);
		assert(agent->get_status() == agent_status::waiting);
		assert(agent->get_waiting_on_id() == 42);

		// leaving waiting clears the target id (see set_status impl)
		agent->set_status(agent_status::idle);
		assert(agent->get_status() == agent_status::idle);
		assert(agent->get_waiting_on_id() == -1);

		agent->set_status(agent_status::error);
		assert(agent->get_status() == agent_status::error);

		agent->set_status(agent_status::dead);
		assert(agent->get_status() == agent_status::dead);
		// dead cascades close() to subagents; ensure no crash on a leaf agent
		agent->close();
		agent.reset();
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 4.
	// set_model / get_model round trip.
	// ------------------------------------------------------------------ 4.
	{
		std::string t = "set_model/get_model";
		auto agent = make_agent(5003, "ModelAgent");
		auto model_a = std::make_shared<ai_model>("model-a", "Model A", "http://localhost:1", "Test", 0.0, 0.0);
		auto model_b = std::make_shared<ai_model>("model-b", "Model B", "http://localhost:1", "Test", 0.0, 0.0);
		agent->set_model(model_b);
		assert(agent->get_model() == model_b);
		assert(agent->get_model()->get_id() == "model-b");
		// set_model(nullptr) is a no-op per implementation
		agent->set_model(nullptr);
		assert(agent->get_model() == model_b);
		agent->close();
		agent.reset();
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 5.
	// Token/cost accounting initial defaults (machine-set elsewhere).
	// These are mostly written by the processing pipeline which we never
	// trigger, so only the documented default values are verified.
	// ------------------------------------------------------------------ 5.
	{
		std::string t = "token/cost defaults";
		auto agent = make_agent(5004, "TokenAgent");
		assert(agent->get_tokens_tx() == 0);
		assert(agent->get_tokens_rx() == 0);
		assert(agent->get_tokens_cached() == 0);
		assert(agent->get_active_tokens() == 0);
		assert(agent->get_estimated_cost() == 0.0);
		// header defaults: -1.0f / -1.0 (unset boundary & duration)
		assert(agent->get_last_boundary_prob() == -1.0f);
		assert(agent->get_last_inference_duration_ms() == -1.0);
		agent->close();
		agent.reset();
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 6.
	// increment_stat / get_stats accumulation and round-trip.
	// ------------------------------------------------------------------ 6.
	{
		std::string t = "increment_stat / get_stats";
		auto agent = make_agent(5005, "StatsAgent");

		auto stats0 = agent->get_stats();
		assert(stats0.find("tool_calls_made") == stats0.end());

		agent->increment_stat("tool_calls_made");
		agent->increment_stat("tool_calls_made");
		agent->increment_stat("tool_calls_made");
		assert(agent->get_stats()["tool_calls_made"] == 3);

		agent->increment_stat("bytes_written", 512);
		agent->increment_stat("bytes_written", 128);
		assert(agent->get_stats()["bytes_written"] == 640);

		agent->increment_stat("single", 7);
		auto stats = agent->get_stats();
		assert(stats["tool_calls_made"] == 3);
		assert(stats["bytes_written"] == 640);
		assert(stats["single"] == 7);

		// get_stats() always overlays token counters as well
		assert(stats["tokens_tx"] == 0);
		agent->close();
		agent.reset();
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 7.
	// get_interactions empty initially; add_interaction round-trip.
	// ------------------------------------------------------------------ 7.
	{
		std::string t = "interactions";
		auto agent = make_agent(5006, "InteractionAgent");
		assert(agent->get_interactions().empty());

		auto inter = std::make_shared<interaction_user_message>("hello from test");
		agent->add_interaction(inter);
		auto interactions = agent->get_interactions();
		assert(interactions.size() == 1);
		assert(interactions[0] == inter);
		assert(interactions[0]->get_raw_text() == "User: hello from test");

		// A second add appends; no dedup happens for distinct objects
		auto inter2 = std::make_shared<interaction_user_message>("second");
		agent->add_interaction(inter2);
		assert(agent->get_interactions().size() == 2);
		agent->close();
		agent.reset();
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 8.
	// read-only / role / properties backing storage interactions.
	// NOTE (surprising behavior discovered): set_role() for summarizer also
	// forces properties_.read_only = true and clears active_families; for all
	// other roles it re-asserts "base" + "git" families. set_read_only() only
	// touches properties_.read_only.
	// ------------------------------------------------------------------ 8.
	{
		std::string t = "read_only/role/properties";
		auto agent = make_agent(5007, "PropsAgent");

		// defaults from agent_properties{}
		assert(!agent->is_read_only());
		assert(agent->get_role() == agent_role::developer);
		auto props = agent->get_properties();
		assert(props.read_only == false);
		assert(props.role == agent_role::developer);
		assert(props.active_families == std::vector<std::string>({"base", "git"}));

		// set_read_only reflects in properties_
		agent->set_read_only(true);
		assert(agent->is_read_only());
		assert(agent->get_properties().read_only == true);
		agent->set_read_only(false);
		assert(!agent->is_read_only());

		// set_role(verifier) updates properties_.role without forcing read-only
		agent->set_role(agent_role::verifier);
		assert(agent->get_role() == agent_role::verifier);
		assert(agent->get_properties().role == agent_role::verifier);
		assert(!agent->is_read_only());

		// set_role(summarizer) forces read_only = true and clears families
		agent->set_role(agent_role::summarizer);
		assert(agent->get_role() == agent_role::summarizer);
		assert(agent->is_read_only());
		assert(agent->get_properties().active_families.empty());

		// set_role(developer) back re-asserts base+git and read_only false
		agent->set_role(agent_role::developer);
		assert(agent->get_role() == agent_role::developer);
		assert(!agent->is_read_only());
		assert(agent->get_properties().active_families == std::vector<std::string>({"base", "git"}));

		// set_properties wholesale round-trip
		agent_properties custom;
		custom.role = agent_role::reviewer;
		custom.read_only = true;
		agent->set_properties(custom);
		auto read_back = agent->get_properties();
		assert(read_back.role == agent_role::reviewer);
		assert(read_back.read_only == true);

		// read_only isolated from role: after set_properties the bare flag
		// getter reads the same backing store
		assert(agent->is_read_only());
		agent->close();
		agent.reset();
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 9.
	// allowed_write_file + is_mutation_possible.
	// NOTE: is_mutation_possible() checks the MODEL's api_type (openai_response
	// disables mutation), NOT is_read_only_! See impl at ai_agent.cpp:121-126.
	// The test model defaults to api_type::openai (mutation allowed). There is
	// no backing field for it, so the instructed read-only toggling asserts
	// only that the getter is stable/consistent.
	// ------------------------------------------------------------------ 9.
	{
		std::string t = "allowed_write_file / is_mutation_possible";
		auto agent = make_agent(5008, "WriteFileAgent");

		assert(agent->get_allowed_write_file().empty());
		agent->set_allowed_write_file("/tmp/only.txt");
		assert(agent->get_allowed_write_file() == "/tmp/only.txt");
		agent->set_allowed_write_file("");
		assert(agent->get_allowed_write_file().empty());

		// default test model is api_type::openai → mutation possible
		assert(agent->is_mutation_possible());
		// toggling read-only does not change mutation possibility in this impl
		agent->set_read_only(true);
		assert(agent->is_mutation_possible());
		agent->set_read_only(false);
		assert(agent->is_mutation_possible());
		agent->close();
		agent.reset();
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 10.
	// Boolean state flags + animation name.
	// ------------------------------------------------------------------ 10.
	{
		std::string t = "state flags & animation";
		auto agent = make_agent(5009, "FlagAgent");

		assert(agent->get_task_description().empty());
		agent->set_task_description("Do a thing");
		assert(agent->get_task_description() == "Do a thing");

		assert(!agent->is_exit_implicitly_on_idle());
		agent->set_exit_implicitly_on_idle(true);
		assert(agent->is_exit_implicitly_on_idle());

		assert(agent->is_notify_parent_on_completion()); // default true
		agent->set_notify_parent_on_completion(false);
		assert(!agent->is_notify_parent_on_completion());

		assert(!agent->is_suppress_parent_injection()); // default false
		agent->set_suppress_parent_injection(true);
		assert(agent->is_suppress_parent_injection());

		assert(agent->get_animation_name() == "default");
		agent->set_animation_name("spinner");
		assert(agent->get_animation_name() == "spinner");
		agent->close();
		agent.reset();
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 11.
	// last_activity_time: monotonic relative ticks >= 0 and strictly growing
	// after update_last_activity_time().
	// ------------------------------------------------------------------ 11.
	{
		std::string t = "last_activity_time";
		auto agent = make_agent(5010, "ActivityAgent");
		long long t0 = agent->get_last_activity_time_ms();
		assert(t0 >= 0);
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		agent->update_last_activity_time();
		long long t1 = agent->get_last_activity_time_ms();
		assert(t1 >= t0);
		assert(t1 - t0 > 0);
		agent->close();
		agent.reset();
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 12.
	// get_current_tool empty on a fresh agent. It is only written by the
	// background processing thread, which we never start, so it stays empty.
	// ------------------------------------------------------------------ 12.
	{
		std::string t = "get_current_tool fresh";
		auto agent = make_agent(5011, "ToolAgent");
		assert(agent->get_current_tool().empty());
		agent->close();
		agent.reset();
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 13.
	// Registry: get_all_active_agents() / find_agent_by_id() after create,
	// and removal after close()/reset() (weak_ptr registry drops dead agents).
	// ------------------------------------------------------------------ 13.
	{
		std::string t = "registry lifecycle";
		int id = 5012;
		auto agent = make_agent(id, "RegistryAgent");

		assert(ai_agent::find_agent_by_id(id) == agent);
		assert(ai_agent::find_agent_by_id(999999) == nullptr);

		bool found = false;
		for (const auto &a : ai_agent::get_all_active_agents()) {
			if (a->get_id() == id) {
				found = true;
			}
		}
		assert(found);

		// close() alone does NOT remove the registry entry (weak_ptr lookup
		// still resolves while the shared_ptr is alive). The entry is dropped
		// lazily when the last shared_ptr is released.
		agent->close();
		assert(ai_agent::find_agent_by_id(id) != nullptr);
		agent.reset();
		assert(ai_agent::find_agent_by_id(id) == nullptr);

		// registry is clean again now
		for (const auto &a : ai_agent::get_all_active_agents()) {
			assert(a->get_id() != id);
		}
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 14.
	// Conversation accessors: get_conversation_data(), get_conversation(),
	// get_current_system_prompt(), get_compaction_segments(),
	// clear_conversation().
	// ------------------------------------------------------------------ 14.
	{
		std::string t = "conversation accessors";
		auto agent = make_agent(5013, "ConvoAgent");

		// fresh agent: non-null Conversation, but empty (no injected context)
		auto convo = agent->get_conversation_data();
		assert(convo != nullptr);
		assert(agent->get_conversation().empty());

		// get_current_system_prompt(): no system prompt was ever injected, so
		// this returns original_system_prompt_, which is empty by default.
		assert(agent->get_current_system_prompt().empty());

		// fresh agent: no episodes → empty segments
		assert(agent->get_compaction_segments().empty());

		// inject some user context and confirm it surfaces
		agent->inject_context("user", "hello stored message");
		auto convo_messages = agent->get_conversation();
		assert(!convo_messages.empty());
		bool saw_hello = false;
		for (const auto &m : convo_messages) {
			if (m.role == "user" && m.content.find("hello stored message") != std::string::npos) {
				saw_hello = true;
			}
		}
		assert(saw_hello);

		// clear_conversation empties the conversation
		agent->clear_conversation();
		assert(agent->get_conversation().empty());
		assert(agent->get_conversation_data() != nullptr);

		agent->close();
		agent.reset();
		print_path(t, true);
	}

	// ------------------------------------------------------------------ 15.
	// spawn_subagent / get_subagents / remove_subagent + parent link.
	// NOTE: spawn_subagent registers the child in the GLOBAL registry via
	// ai_agent::create(); cleanup closes BOTH parent and child so no stale
	// registry entries remain for later tests.
	// ------------------------------------------------------------------ 15.
	{
		std::string t = "subagent lifecycle";
		int parent_id = 5014;
		auto parent = make_agent(parent_id, "ParentAgent");

		assert(parent->get_subagents().empty());

		auto sub = parent->spawn_subagent("child task");
		assert(sub != nullptr);
		// child registered in global registry and linked back to parent
		assert(sub->get_id() != parent_id);
		assert(sub->get_parent() == parent);

		auto subs = parent->get_subagents();
		assert(subs.size() == 1);
		assert(subs[0] == sub);
		assert(ai_agent::find_agent_by_id(sub->get_id()) == sub);

		// spawn a second child to test removal of just one
		auto sub2 = parent->spawn_subagent("child task 2");
		assert(parent->get_subagents().size() == 2);

		parent->remove_subagent(sub->get_id());
		assert(parent->get_subagents().size() == 1);
		assert(parent->get_subagents()[0] == sub2);
		// drop the earlier snapshot vector: it holds strong refs to both
		// children that would otherwise keep them alive in the registry
		subs.clear();

		// remove_subagent does NOT unregister the child from the global
		// registry; only releasing the shared_ptr drops it (weak_ptr registry).
		int sub_id = sub->get_id();
		assert(ai_agent::find_agent_by_id(sub_id) == sub);
		sub->close();
		sub.reset();
		assert(ai_agent::find_agent_by_id(sub_id) == nullptr);

		// cleanup: close both remaining agents
		sub2->close();
		sub2.reset();
		parent->close();
		parent.reset();

		// registry fully clean
		assert(ai_agent::find_agent_by_id(parent_id) == nullptr);
		print_path(t, true);
	}

	std::cout << "test_ai_agent_state: ALL TESTS PASSED" << std::endl;
	return 0;
}
