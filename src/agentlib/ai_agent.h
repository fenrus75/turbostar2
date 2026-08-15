#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include "agent_properties.h"
#include "ai_model.h"
#include "document_provider.h"
#include "interactions/interactions.h"
#include "llm_client.h"
#include "tool_registry.h"

class event_queue;

namespace agentlib
{
class Conversation;

enum class agent_status { idle, thinking, tool_execution, waiting, error, dead };

std::string agent_status_to_string(agent_status status, const std::string &tool_name = "");
std::string agent_status_to_name(agent_status status);


struct episode_index_entry {
	std::string id;
	std::string title;
	std::string summary;
	std::string reactivation_hint;
	std::vector<std::string> tags;
	long long episode_seq{0};
	long long lru_seq{0};

	// Exact token estimates calculated at serialization time
	int tokens_level_0{0}; // Raw
	int tokens_level_1{0}; // Think-Free (Native)
	int tokens_level_2{0}; // Think-Free (Native + Pseudo)
};

struct compaction_segment {
	std::string label;
	int uncompacted_tokens{0};
	int current_level{0};
};

class ai_agent : public std::enable_shared_from_this<ai_agent>
{
      public:
	static std::shared_ptr<ai_agent> create(int id, const std::string &name, std::shared_ptr<ai_model> model, event_queue *queue,
						document_provider *doc_provider);
	~ai_agent();

	void submit_prompt(const std::string &prompt_text);
	void inject_context(const std::string &role, const std::string &content, bool trigger_processing = false);
	void replace_tool_result(const std::string &tool_call_id, const std::string &new_content);
	void cancel_current_task();
	void close();

	int get_id() const
	{
		return id_;
	}
	std::string get_name() const
	{
		return name_;
	}
	agent_status get_status() const
	{
		return status_;
	}
	std::string get_current_tool() const
	{
		// current_tool_ is written from the background processing thread and read from UI
		// threads, so guard it against a read/write data race on std::string.
		std::lock_guard<std::mutex> lock(current_tool_mutex_);
		return current_tool_;
	}

	// Explicitly set the status, optionally with a target ID if waiting
	void set_status(agent_status s, int target_id = -1);

	// Blocks until the agent's status is idle or error
	void wait_until_idle();
	bool wait_until_idle_for(std::chrono::milliseconds timeout);

	int get_waiting_on_id() const
	{
		return waiting_on_id_;
	}


	std::shared_ptr<ai_agent> spawn_subagent(const std::string &task_description);
	void remove_subagent(int id);
	std::vector<std::shared_ptr<ai_agent>> get_subagents() const;

	static std::shared_ptr<ai_agent> find_agent_by_id(int id);
	static std::vector<std::shared_ptr<ai_agent>> get_all_active_agents();

	void clear_conversation();

	void set_model(std::shared_ptr<ai_model> model);
	std::shared_ptr<ai_model> get_model() const
	{
		return model_;
	}
	int get_tokens_tx() const
	{
		return tokens_tx_;
	}
	int get_tokens_rx() const
	{
		return tokens_rx_;
	}
	int get_tokens_cached() const
	{
		return tokens_cached_;
	}
	int get_active_tokens() const
	{
		return active_tokens_.load();
	}
	std::vector<compaction_segment> get_compaction_segments() const;
	std::string get_current_system_prompt() const;
	double get_estimated_cost() const
	{
		return estimated_cost_;
	}
	float get_last_boundary_prob() const
	{
		return last_boundary_prob_.load();
	}
	double get_last_inference_duration_ms() const
	{
		return last_inference_duration_ms_.load();
	}
	void add_active_skill(const std::string &skill_name);
	bool activate_skill(const std::string &skill_name);
	std::vector<std::string> get_active_skills() const;
	void add_active_tool_family(const std::string &family_name);
	std::vector<std::string> get_active_tool_families() const;
	bool is_tool_family_active(const std::string &family_name) const;

	void increment_stat(const std::string &key, int amount = 1);
	std::map<std::string, int> get_stats() const;

	std::vector<std::shared_ptr<agent_interaction>> get_interactions() const;
	void add_interaction(std::shared_ptr<agent_interaction> interaction);

	bool is_read_only() const;
	void set_read_only(bool ro);

	agent_role get_role() const;
	void set_role(agent_role r);

	agent_properties get_properties() const;
	void set_properties(const agent_properties &props);

	std::string get_allowed_write_file() const;
	void set_allowed_write_file(const std::string &path);

	bool is_mutation_possible() const;

	void set_parent(std::weak_ptr<ai_agent> parent)
	{
		parent_agent_ = std::move(parent);
	}
	std::shared_ptr<ai_agent> get_parent() const
	{
		return parent_agent_.lock();
	}
	event_queue *get_global_queue() const
	{
		return global_queue_;
	}

	void save_conversation(const std::string &filepath) const;
	void page_out_context(size_t start_index, size_t end_index, std::string_view title, std::string_view summary,
			      const std::vector<std::string> &tags);
	void page_out_prior_context(std::string_view target_episode_id, bool include_all_prior, std::string_view title,
				    std::string_view summary, const std::vector<std::string> &tags);
	void snapshot_episode(std::string_view title, std::string_view summary, const std::vector<std::string> &tags);
	void update_episode_hint(const std::string &episode_id, const std::string &hint);
	bool page_in_context(const std::string &episode_id, int compression_level = 1);
	bool set_episode_state(const std::string &episode_id, int target_level);
	std::vector<std::string> page_in_history_auto(int default_level = 1, double target_fraction = 0.5);
	int calculate_current_tokens() const;

	void save_active_state() const;
	bool load_active_state(bool fresh_agent = false);
	void load_episode_index();
	std::string get_memory_index() const;
	void set_final_result(const std::string &result);
	std::string get_final_result() const;
	bool has_final_result() const;

	std::string get_task_description() const;
	void set_task_description(const std::string &desc);
	void set_exit_implicitly_on_idle(bool val);
	bool is_exit_implicitly_on_idle() const;
	void set_notify_parent_on_completion(bool val);
	bool is_notify_parent_on_completion() const;

	// When set, agent tool calls made by this agent must not inject notifications into the
	// parent's conversation stream. Used by orchestration (e.g. synchronous code review) where
	// results are delivered via a toolcall return instead, so per-item injections would be
	// redundant noise.
	void set_suppress_parent_injection(bool val);
	bool is_suppress_parent_injection() const;

	std::string get_animation_name() const;
	void set_animation_name(const std::string &name);

	// Returns the last activity timestamp in milliseconds. NOTE: this is not a wall-clock
	// timestamp; it is std::chrono::steady_clock::now().time_since_epoch() expressed in
	// milliseconds (a monotonic relative tick count). Consumers (e.g. the UI) must only use
	// it for relative comparisons against values produced by the same steady_clock basis.
	long long get_last_activity_time_ms() const
	{
		return last_activity_time_ms_.load();
	}
	void update_last_activity_time()
	{
		auto now = std::chrono::steady_clock::now().time_since_epoch();
		last_activity_time_ms_.store(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
	}
	std::map<std::string, episode_index_entry> get_episode_index() const
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		return episode_index_;
	}
	void inject_archived_episodes_summary();
	void compact_ephemeral_errors(std::vector<message> &convo);
	void evaluate_auto_episode(std::vector<message> &convo);
	void evaluate_compaction();
	void force_compaction();

	std::vector<message> get_conversation() const;
	void set_conversation(std::span<const message> c);
	std::shared_ptr<Conversation> get_conversation_data() const;

       protected:
	int id_;
	std::string name_;
	std::shared_ptr<ai_model> model_;
	event_queue *global_queue_{nullptr};
	document_provider *doc_provider_{nullptr};
	std::atomic<agent_status> status_{agent_status::idle};
	int waiting_on_id_{-1};
	bool is_read_only_{false};
	std::atomic<bool> is_closed_{false};

	/*
	 * state_mutex_ protects the agent's interactive state and lifecycle resources,
	 * including subagents_, active_skills_, original_system_prompt_, interactions_,
	 * final_result_, exit_implicitly_on_idle_, notify_parent_on_completion_,
	 * animation_name_, and allowed_write_file_.
	 * Locking Rules:
	 * - Held during status changes, subagent spawning/management,
	 *   modifications to the implicit exit and notification flags, animation name changes,
	 *   and reads/writes of the allowed_write_file_ restriction.
	 * - status_cv_ is used in conjunction with state_mutex_ for waiting until the agent is idle.
	 */
	mutable std::mutex state_mutex_;
	std::condition_variable status_cv_;
	std::vector<std::shared_ptr<ai_agent>> subagents_;
	uint64_t subagent_sequence_counter_{0};
	std::vector<std::string> active_skills_;
	std::string original_system_prompt_;
	std::string final_result_;
	bool exit_implicitly_on_idle_{false};
	bool notify_parent_on_completion_{true};
	bool suppress_parent_injection_{false};
	std::string animation_name_{"default"};

	void archive_to_cache() const;

      private:
	ai_agent(int id, const std::string &name, std::shared_ptr<ai_model> model, event_queue *queue, document_provider *doc_provider);

	void start_processing();

	struct pending_summary {
		std::string episode_id;
		std::string filepath;
	};
	void summary_worker_loop();

	// current_tool_ is written from the background processing thread and read from UI threads;
	// current_tool_mutex_ guards it against read/write races on the std::string.
	std::string current_tool_;
	mutable std::mutex current_tool_mutex_;

	std::weak_ptr<ai_agent> parent_agent_;

	void update_system_prompt_with_families();
	void set_conversation_unlocked(std::span<const message> c);
	std::vector<message> get_conversation_unlocked() const;

	std::atomic<int> tokens_tx_{0};
	std::atomic<int> tokens_rx_{0};
	std::atomic<int> tokens_cached_{0};
	std::atomic<int> active_tokens_{0};
	std::atomic<double> estimated_cost_{0.0};

	/*
	 * stats_mutex_ protects the stats_ statistics map.
	 * Locking Rules:
	 * - Held briefly when incrementing or querying stats.
	 */
	mutable std::mutex stats_mutex_;
	std::map<std::string, int> stats_;

	/*
	 * conversation_mutex_ protects conversation_ history, episode_index_ maps,
	 * the client_ object, and the last_response_id_ stateful tracking ID.
	 * Locking Rules:
	 * - Held during conversation serialization, prompt submission, compaction evaluation,
	 *   and memory/episode loading or paging operations.
	 */
	mutable std::mutex conversation_mutex_;
	std::shared_ptr<Conversation> conversation_;
	std::string last_response_id_;
	std::map<std::string, episode_index_entry> episode_index_;
	std::shared_ptr<llm_client> client_;
	std::shared_ptr<llm_transport> background_transport_;

	/*
	 * background_transport_mutex_ protects the active background transport object.
	 * Locking Rules:
	 * - Held during background task creation to prevent concurrent transport modification.
	 */
	mutable std::mutex background_transport_mutex_;

	/*
	 * summary_mutex_ protects the summary_queue_ and controls the lifecycle
	 * of the asynchronous summary worker thread (summary_thread_).
	 * Locking Rules:
	 * - Held briefly when pushing a new summary task to the queue and popping it inside the worker loop.
	 * - Used in conjunction with summary_cv_ to signal the summary worker thread.
	 */
	std::mutex summary_mutex_;
	std::condition_variable summary_cv_;
	std::vector<pending_summary> summary_queue_;
	std::thread summary_thread_;

	std::atomic<long long> next_lru_seq_{1};
	std::atomic<float> last_boundary_prob_{-1.0f};
	std::atomic<double> last_inference_duration_ms_{-1.0};
	std::atomic<long long> last_activity_time_ms_{0};

	std::string task_description_;

	/*
	 * properties_mutex_ protects the agent's role/profile properties inside the
	 * properties_ struct (properties_.read_only, properties_.role, and
	 * properties_.active_families), accessed via get/set_properties(),
	 * get/set_read_only(), get/set_role(), and the tool family helpers.
	 * Locking Rules:
	 * - Held briefly when reading or mutating the properties_ struct.
	 */
	mutable std::mutex properties_mutex_;
	agent_properties properties_;
	std::string allowed_write_file_;
};

} // namespace agentlib