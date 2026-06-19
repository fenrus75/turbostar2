#pragma once

#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include "episode.h"
#include "../ai_model.h"

namespace agentlib {

class Conversation {
public:
	Conversation();

	std::shared_ptr<Episode> get_current_episode() const;
	void set_current_episode(std::shared_ptr<Episode> ep);

	const std::vector<std::shared_ptr<Episode>>& get_episodes() const { return episodes_; }
	void add_episode(std::shared_ptr<Episode> ep);
	void clear_episodes();

	void archive_current_episode(const std::string& title, const std::string& summary);
	std::shared_ptr<Episode> create_new_episode(const std::string& id, const std::string& title, const std::string& summary);

	std::shared_ptr<ai_model> get_model() const;
	void set_model(std::shared_ptr<ai_model> model);

	std::string get_current_world_view() const;
	void set_current_world_view(std::string world_view);

	bool is_history_invalidated() const { return history_invalidated_; }
	void invalidate_history(bool invalid) { history_invalidated_ = invalid; }

	nlohmann::json get_connection_state() const;
	void set_connection_state(nlohmann::json state);

	long long get_next_episode_seq() const;
	void set_next_episode_seq(long long seq);
	long long allocate_next_episode_seq();

	long long get_next_turn_seq() const;
	void set_next_turn_seq(long long seq);
	long long allocate_next_turn_seq();

	std::vector<std::shared_ptr<Turn>> get_turns_since(long long sequence_number) const;

	int estimate_token_count() const;
	time_range get_time_range() const;

	void add_transaction(std::shared_ptr<Transaction> tx);
	void append_to_current_turn(const std::string& chunk);
	void append_to_current_reasoning(const std::string& chunk);

	nlohmann::json serialize() const;
	static std::shared_ptr<Conversation> deserialize(const nlohmann::json& j);

private:
	/*
	 * mutex_ protects the thread-safe operations on the Conversation object's
	 * internal episode collection (episodes_ and current_episode_) and settings
	 * (model_, world_view_, and history_invalidated_).
	 * Locking Rules:
	 * - Held briefly during getters/setters/appends to prevent concurrent mutations.
	 */
	mutable std::mutex mutex_;
	std::vector<std::shared_ptr<Episode>> episodes_;
	std::shared_ptr<Episode> current_episode_;
	std::shared_ptr<ai_model> model_;
	std::string world_view_;
	bool history_invalidated_{false};
	nlohmann::json connection_state_;
	long long next_episode_seq_{1};
	long long next_turn_seq_{1};
};

} // namespace agentlib
