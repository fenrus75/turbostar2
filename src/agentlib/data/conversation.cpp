#include "conversation.h"
#include "model_response_turn.h"
#include <algorithm>

namespace agentlib {

Conversation::Conversation() {}

std::shared_ptr<Episode> Conversation::get_current_episode() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return current_episode_;
}

void Conversation::set_current_episode(std::shared_ptr<Episode> ep) {
	std::lock_guard<std::mutex> lock(mutex_);
	current_episode_ = std::move(ep);
}

void Conversation::add_episode(std::shared_ptr<Episode> ep) {
	std::lock_guard<std::mutex> lock(mutex_);
	/*
	 * Enforce the episode invariant: any historical episode added to the episodes_
	 * collection must be marked as finalized.
	 */
	if (ep) {
		ep->set_finalized(true);
	}
	episodes_.push_back(std::move(ep));
}

void Conversation::clear_episodes() {
	std::lock_guard<std::mutex> lock(mutex_);
	episodes_.clear();
	current_episode_.reset();
}

void Conversation::archive_current_episode(const std::string& title, const std::string& summary) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (current_episode_) {
		current_episode_->set_title(title);
		current_episode_->set_summary(summary);
		current_episode_->set_compaction_level(COMPACTION_LEVEL_PAGED_OUT); // paged-out/archived
		/*
		 * Enforce the episode invariant: when archiving the current active episode,
		 * it transitions to a historical/archived state and becomes finalized.
		 */
		current_episode_->set_finalized(true);
		episodes_.push_back(current_episode_);
		current_episode_.reset();
	}
}

std::shared_ptr<Episode> Conversation::create_new_episode(const std::string& id, const std::string& title, const std::string& summary) {
	std::lock_guard<std::mutex> lock(mutex_);
	current_episode_ = std::make_shared<Episode>(id, title, summary);
	return current_episode_;
}

std::shared_ptr<ai_model> Conversation::get_model() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return model_;
}

void Conversation::set_model(std::shared_ptr<ai_model> model) {
	std::lock_guard<std::mutex> lock(mutex_);
	model_ = std::move(model);
	history_invalidated_ = true; // Invalidate session on model change
}

std::string Conversation::get_current_world_view() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return world_view_;
}

void Conversation::set_current_world_view(std::string world_view) {
	std::lock_guard<std::mutex> lock(mutex_);
	world_view_ = std::move(world_view);
}

nlohmann::json Conversation::get_connection_state() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return connection_state_;
}

void Conversation::set_connection_state(nlohmann::json state) {
	std::lock_guard<std::mutex> lock(mutex_);
	connection_state_ = std::move(state);
}

int Conversation::estimate_token_count() const {
	std::lock_guard<std::mutex> lock(mutex_);
	int sum = 0;
	for (const auto& ep : episodes_) {
		sum += ep->estimate_token_count(ep->get_compaction_level());
	}
	if (current_episode_) {
		sum += current_episode_->estimate_token_count(current_episode_->get_compaction_level());
	}
	return sum;
}

time_range Conversation::get_time_range() const {
	std::lock_guard<std::mutex> lock(mutex_);
	if (episodes_.empty() && !current_episode_) return {};
	time_range consolidated{ UINT64_MAX, 0 };
	for (const auto& ep : episodes_) {
		time_range r = ep->get_time_range();
		consolidated.start_time = std::min(consolidated.start_time, r.start_time);
		consolidated.end_time = std::max(consolidated.end_time, r.end_time);
	}
	if (current_episode_) {
		time_range r = current_episode_->get_time_range();
		consolidated.start_time = std::min(consolidated.start_time, r.start_time);
		consolidated.end_time = std::max(consolidated.end_time, r.end_time);
	}
	return consolidated;
}

void Conversation::add_transaction(std::shared_ptr<Transaction> tx) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (current_episode_) {
		current_episode_->add_transaction(std::move(tx));
	}
}

void Conversation::append_to_current_turn(const std::string& chunk) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!current_episode_ || current_episode_->get_transactions().empty()) return;
	auto tx = current_episode_->get_transactions().back();
	if (tx->get_turns().empty()) return;
	tx->get_turns().back()->append_content(chunk);
}

void Conversation::append_to_current_reasoning(const std::string& chunk) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!current_episode_ || current_episode_->get_transactions().empty()) return;
	auto tx = current_episode_->get_transactions().back();
	if (tx->get_turns().empty()) return;
	auto model_turn = std::dynamic_pointer_cast<model_response_turn>(tx->get_turns().back());
	if (model_turn) {
		model_turn->append_reasoning_content(chunk);
	}
}

nlohmann::json Conversation::serialize() const {
	std::lock_guard<std::mutex> lock(mutex_);
	nlohmann::json j;
	if (model_) {
		j["model_id"] = model_->get_id();
	}
	j["world_view"] = world_view_;
	j["history_invalidated"] = history_invalidated_;
	j["next_episode_seq"] = next_episode_seq_;
	j["next_turn_seq"] = next_turn_seq_;
	if (!connection_state_.is_null()) {
		j["connection_state"] = connection_state_;
	}
	nlohmann::json ep_array = nlohmann::json::array();
	for (const auto& ep : episodes_) {
		ep_array.push_back(ep->serialize());
	}
	j["episodes"] = ep_array;
	if (current_episode_) {
		j["current_episode"] = current_episode_->serialize();
	}
	return j;
}

std::shared_ptr<Conversation> Conversation::deserialize(const nlohmann::json& j) {
	auto convo = std::make_shared<Conversation>();
	convo->world_view_ = j.value("world_view", "");
	convo->history_invalidated_ = j.value("history_invalidated", false);
	convo->next_episode_seq_ = j.value("next_episode_seq", 1LL);
	convo->next_turn_seq_ = j.value("next_turn_seq", 1LL);
	if (j.contains("connection_state")) {
		convo->connection_state_ = j["connection_state"];
	}
	
	// Load active model if registered
	std::string model_id = j.value("model_id", "");
	if (!model_id.empty()) {
		convo->model_ = ai_model_registry::get_instance().get_model(model_id);
	}

	if (j.contains("episodes") && j["episodes"].is_array()) {
		for (const auto& item : j["episodes"]) {
			convo->add_episode(Episode::deserialize(item));
		}
	}
	if (j.contains("current_episode") && !j["current_episode"].is_null()) {
		convo->set_current_episode(Episode::deserialize(j["current_episode"]));
	}
	return convo;
}

/*
 * Returns the next sequence number to be allocated for an episode.
 */
long long Conversation::get_next_episode_seq() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return next_episode_seq_;
}

/*
 * Sets the next sequence number, which is required when restoring index entries
 * or metadata sidecars on startup.
 */
void Conversation::set_next_episode_seq(long long seq) {
	std::lock_guard<std::mutex> lock(mutex_);
	next_episode_seq_ = seq;
}

/*
 * Allocates and returns a unique monotonic sequence number for a new episode.
 */
long long Conversation::allocate_next_episode_seq() {
	std::lock_guard<std::mutex> lock(mutex_);
	return next_episode_seq_++;
}

/*
 * Returns the next sequence number to be allocated for a turn.
 */
long long Conversation::get_next_turn_seq() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return next_turn_seq_;
}

/*
 * Sets the next sequence number for turns.
 */
void Conversation::set_next_turn_seq(long long seq) {
	std::lock_guard<std::mutex> lock(mutex_);
	next_turn_seq_ = seq;
}

/*
 * Allocates and returns a unique monotonic sequence number for a new turn.
 */
long long Conversation::allocate_next_turn_seq() {
	std::lock_guard<std::mutex> lock(mutex_);
	return next_turn_seq_++;
}

/*
 * Returns a vector of all turns with sequence numbers strictly greater than sequence_number.
 */
std::vector<std::shared_ptr<Turn>> Conversation::get_turns_since(long long sequence_number) const {
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<std::shared_ptr<Turn>> result;
	
	auto collect_from_episode = [&](const std::shared_ptr<Episode>& ep) {
		if (!ep) return;
		if (ep->get_max_turn() <= sequence_number) return;
		for (const auto& tx : ep->get_transactions()) {
			if (tx->get_max_turn() <= sequence_number) continue;
			for (const auto& turn : tx->get_turns()) {
				if (turn->get_sequence_number() > sequence_number) {
					result.push_back(turn);
				}
			}
		}
	};

	for (const auto& ep : episodes_) {
		collect_from_episode(ep);
	}
	collect_from_episode(current_episode_);
	
	std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
		return a->get_sequence_number() < b->get_sequence_number();
	});
	
	return result;
}

} // namespace agentlib
