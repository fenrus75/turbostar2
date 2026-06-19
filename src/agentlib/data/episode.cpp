#include "episode.h"
#include <sstream>
#include <algorithm>

namespace agentlib {

Episode::Episode(std::string id, std::string title, std::string summary)
	: id_(std::move(id)), title_(std::move(title)), summary_(std::move(summary)) {}

std::vector<message> Episode::to_messages(const model_capabilities& caps, bool include_anchor) const {
	std::stringstream pointer_msg;
	pointer_msg << "[SYSTEM MEMORY: Episode Archived]\n";
	pointer_msg << "Title: " << title_ << "\n";
	pointer_msg << "Summary: " << summary_ << "\n";
	if (!reactivation_hint_.empty()) {
		pointer_msg << "Demand-Load Hint: " << reactivation_hint_ << "\n";
	}
	
	if (compaction_level_ == COMPACTION_LEVEL_PAGED_OUT) {
		pointer_msg << "Raw history archive: " << id_;
		message anchor;
		anchor.role = "system";
		anchor.content = pointer_msg.str();
		anchor.episode_id = id_;
		anchor.episode_level = COMPACTION_LEVEL_PAGED_OUT;
		return {anchor};
	} else {
		std::vector<message> messages;
		if (include_anchor) {
			pointer_msg << "Raw history page-in: " << id_ << " level: " << compaction_level_;
			message anchor;
			anchor.role = "system";
			anchor.content = pointer_msg.str();
			anchor.episode_id = id_;
			anchor.episode_level = compaction_level_;
			messages.push_back(anchor);
		}
		
		for (const auto& tx : transactions_) {
			auto tx_msgs = tx->to_messages(caps, compaction_level_);
			for (auto& m : tx_msgs) {
				m.episode_id = id_;
				m.episode_level = compaction_level_;
			}
			messages.insert(messages.end(), tx_msgs.begin(), tx_msgs.end());
		}
		return messages;
	}
}

void Episode::add_transaction(std::shared_ptr<Transaction> tx) {
	long long tx_min = tx->get_min_turn();
	long long tx_max = tx->get_max_turn();
	if (tx_min > 0) {
		if (min_turn_ == 0 || tx_min < min_turn_) {
			min_turn_ = tx_min;
		}
	}
	if (tx_max > 0) {
		if (max_turn_ == 0 || tx_max > max_turn_) {
			max_turn_ = tx_max;
		}
	}
	transactions_.push_back(std::move(tx));
}

void Episode::insert_transaction(size_t index, std::shared_ptr<Transaction> tx) {
	if (index > transactions_.size()) {
		index = transactions_.size();
	}
	long long tx_min = tx->get_min_turn();
	long long tx_max = tx->get_max_turn();
	if (tx_min > 0) {
		if (min_turn_ == 0 || tx_min < min_turn_) {
			min_turn_ = tx_min;
		}
	}
	if (tx_max > 0) {
		if (max_turn_ == 0 || tx_max > max_turn_) {
			max_turn_ = tx_max;
		}
	}
	transactions_.insert(transactions_.begin() + index, std::move(tx));
}

long long Episode::get_min_turn() const {
	long long val = min_turn_;
	for (const auto& tx : transactions_) {
		long long tx_min = tx->get_min_turn();
		if (tx_min > 0) {
			if (val == 0 || tx_min < val) {
				val = tx_min;
			}
		}
	}
	return val;
}

long long Episode::get_max_turn() const {
	long long val = max_turn_;
	for (const auto& tx : transactions_) {
		long long tx_max = tx->get_max_turn();
		if (tx_max > 0) {
			if (val == 0 || tx_max > val) {
				val = tx_max;
			}
		}
	}
	return val;
}

int Episode::estimate_token_count(int compaction_level) const {
	int summary_tokens = static_cast<int>((title_.size() + summary_.size()) / 4 + 50);
	if (compaction_level == COMPACTION_LEVEL_PAGED_OUT) {
		return summary_tokens;
	}
	int sum = summary_tokens;
	for (const auto& tx : transactions_) {
		sum += tx->estimate_token_count(compaction_level);
	}
	return sum;
}

time_range Episode::get_time_range() const {
	if (transactions_.empty()) return {};
	time_range consolidated{ UINT64_MAX, 0 };
	for (const auto& tx : transactions_) {
		time_range r = tx->get_time_range();
		consolidated.start_time = std::min(consolidated.start_time, r.start_time);
		consolidated.end_time = std::max(consolidated.end_time, r.end_time);
	}
	return consolidated;
}

nlohmann::json Episode::serialize() const {
	nlohmann::json j;
	j["id"] = id_;
	j["title"] = title_;
	j["summary"] = summary_;
	j["compaction_level"] = compaction_level_;
	j["finalized"] = finalized_;
	j["sequence_number"] = sequence_number_;
	j["reactivation_hint"] = reactivation_hint_;
	j["min_turn"] = get_min_turn();
	j["max_turn"] = get_max_turn();
	nlohmann::json tx_array = nlohmann::json::array();
	for (const auto& tx : transactions_) {
		tx_array.push_back(tx->serialize());
	}
	j["transactions"] = tx_array;
	return j;
}

std::shared_ptr<Episode> Episode::deserialize(const nlohmann::json& j) {
	std::string id = j.at("id").get<std::string>();
	std::string title = j.value("title", "");
	std::string summary = j.value("summary", "");
	auto ep = std::make_shared<Episode>(id, title, summary);
	ep->compaction_level_ = j.value("compaction_level", 0);
	ep->finalized_ = j.value("finalized", false);
	ep->set_sequence_number(j.value("sequence_number", 0LL));
	ep->reactivation_hint_ = j.value("reactivation_hint", "");
	ep->min_turn_ = j.value("min_turn", 0LL);
	ep->max_turn_ = j.value("max_turn", 0LL);
	if (j.contains("transactions") && j["transactions"].is_array()) {
		for (const auto& item : j["transactions"]) {
			ep->add_transaction(Transaction::deserialize(item));
		}
	}
	return ep;
}

/*
 * Copies the state of another Episode into this instance, which is required
 * when demand-loading paged-out episodes from disk storage back into active memory.
 */
void Episode::copy_from(const Episode& other) {
	title_ = other.title_;
	summary_ = other.summary_;
	reactivation_hint_ = other.reactivation_hint_;
	finalized_ = other.finalized_;
	set_sequence_number(other.sequence_number_);
	transactions_ = other.transactions_;
	min_turn_ = other.min_turn_;
	max_turn_ = other.max_turn_;
}

/*
 * Clears all loaded transactions from memory. This is used to free memory
 * when archiving or paging out an episode to a level where the raw transaction details
 * are not needed in active memory.
 */
void Episode::clear_transactions() {
	transactions_.clear();
}

} // namespace agentlib
