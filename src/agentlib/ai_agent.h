#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "ai_model.h"
#include "document_provider.h"
#include "interactions/interactions.h"
#include "llm_client.h"
#include "tool_registry.h"

class event_queue;

namespace agentlib
{

enum class agent_status { idle, thinking, tool_execution, waiting, error };

std::string agent_status_to_string(agent_status status, const std::string &tool_name = "");

struct todo_item {
	std::string text;
	bool completed{false};
	int reminder_count{0};
};

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
	static void coalesce_tool_calls(std::vector<tool_call> &tool_calls, std::unordered_map<std::string, std::string> &merged_to_parent,
					std::unordered_map<std::string, std::pair<int, int>> &parent_ranges);
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
		return current_tool_;
	}

	// Explicitly set the status, optionally with a target ID if waiting
	void set_status(agent_status s, int target_id = -1);

	// Blocks until the agent's status is idle or error
	void wait_until_idle();

	int get_waiting_on_id() const
	{
		return waiting_on_id_;
	}

	void add_todo(const std::string &task);
	std::vector<todo_item> get_todos() const;
	std::optional<std::string> pop_todo();
	bool mark_todo_complete(const std::string &text_match, std::string &out_error);
	bool delete_todo(const std::string &text_match, std::string &out_error);
	std::string get_todo_reminder_msg();

	std::shared_ptr<ai_agent> spawn_subagent(const std::string &task_description);
	void remove_subagent(int id);
	std::vector<std::shared_ptr<ai_agent>> get_subagents() const;

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
	std::vector<std::string> get_active_skills() const;
	void add_active_tool_family(const std::string &family_name);
	std::vector<std::string> get_active_tool_families() const;
	bool is_tool_family_active(const std::string &family_name) const;

	void increment_stat(const std::string &key, int amount = 1);
	std::map<std::string, int> get_stats() const;

	const std::vector<std::shared_ptr<agent_interaction>> &get_interactions() const
	{
		return interactions_;
	}
	void add_interaction(std::shared_ptr<agent_interaction> interaction);

	bool is_read_only() const
	{
		return read_only_;
	}
	void set_read_only(bool ro)
	{
		read_only_ = ro;
	}

	bool is_planning() const
	{
		return is_planning_.load();
	}
	void set_planning(bool planning, size_t start_index = 0)
	{
		is_planning_.store(planning);
		if (planning)
			planning_start_index_ = start_index;
		else
			set_plan_file("");
	}
	size_t get_planning_start_index() const
	{
		return planning_start_index_;
	}

	std::string get_plan_file() const
	{
		std::lock_guard<std::mutex> lock(planning_mutex_);
		return plan_file_;
	}
	void set_plan_file(const std::string &file)
	{
		std::lock_guard<std::mutex> lock(planning_mutex_);
		plan_file_ = file;
	}

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
	void page_out_context(size_t start_index, size_t end_index, const std::string &title, const std::string &summary,
			      const std::vector<std::string> &tags);
	void page_out_prior_context(const std::string &target_episode_id, bool include_all_prior, const std::string &title,
				    const std::string &summary, const std::vector<std::string> &tags);
	void snapshot_episode(const std::string &title, const std::string &summary, const std::vector<std::string> &tags);
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
	std::map<std::string, episode_index_entry> get_episode_index() const
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		return episode_index_;
	}
	void inject_archived_episodes_summary();
	void compact_ephemeral_errors(std::vector<message> &convo);
	void evaluate_auto_episode(std::vector<message> &convo);
	void evaluate_compaction();

	std::vector<message> get_conversation() const
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		return conversation_;
	}
	void set_conversation(const std::vector<message> &c)
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		conversation_ = c;
	}

      private:
	ai_agent(int id, const std::string &name, std::shared_ptr<ai_model> model, event_queue *queue, document_provider *doc_provider);

	void start_processing();

	struct pending_summary {
		std::string episode_id;
		std::string filepath;
	};
	void summary_worker_loop();

	int id_;
	std::string name_;
	std::shared_ptr<ai_model> model_;
	std::atomic<agent_status> status_{agent_status::idle};
	std::string current_tool_;
	std::atomic<bool> is_closed_{false};
	std::atomic<bool> read_only_{false};
	std::atomic<bool> is_planning_{false};
	size_t planning_start_index_{0};

	/*
	 * planning_mutex_ protects the plan_file_ path string.
	 * Locking Rules:
	 * - Held briefly during get_plan_file() and set_plan_file() accesses.
	 */
	mutable std::mutex planning_mutex_;
	std::string plan_file_;
	std::atomic<int> waiting_on_id_{-1};

	std::weak_ptr<ai_agent> parent_agent_;

	event_queue *global_queue_;
	document_provider *doc_provider_;

	/*
	 * state_mutex_ protects the agent's interactive state and lifecycle resources,
	 * including todos_, subagents_, active_skills_, active_tool_families_, original_system_prompt_,
	 * interactions_, and final_result_.
	 * Locking Rules:
	 * - Held during status changes, subagent spawning/management, and todo list modifications.
	 * - status_cv_ is used in conjunction with state_mutex_ for waiting until the agent is idle.
	 */
	std::mutex state_mutex_;
	std::condition_variable status_cv_;
	std::vector<todo_item> todos_;
	std::vector<std::shared_ptr<ai_agent>> subagents_;
	std::vector<std::string> active_skills_;
	std::vector<std::string> active_tool_families_;
	std::string original_system_prompt_;
	std::vector<std::shared_ptr<agent_interaction>> interactions_;
	std::string final_result_;

      private:
	void update_system_prompt_with_families();
	void save_todos_internal() const;
	void save_todos_internal_unlocked() const;

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
	 * and the client_ object.
	 * Locking Rules:
	 * - Held during conversation serialization, prompt submission, compaction evaluation,
	 *   and memory/episode loading or paging operations.
	 */
	mutable std::mutex conversation_mutex_;
	std::vector<message> conversation_;
	std::map<std::string, episode_index_entry> episode_index_;
	std::unique_ptr<llm_client> client_;
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

	std::atomic<long long> next_episode_seq_{1};
	std::atomic<long long> next_lru_seq_{1};
	std::atomic<float> last_boundary_prob_{-1.0f};
	std::atomic<double> last_inference_duration_ms_{-1.0};
};

} // namespace agentlib