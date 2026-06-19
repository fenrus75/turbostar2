#include "transaction.h"
#include "turn_registry.h"
#include <sstream>
#include <algorithm>

namespace agentlib {

Transaction::Transaction(std::string id, transaction_type type)
	: id_(std::move(id)), type_(type) {}

std::vector<message> Transaction::to_messages(const model_capabilities& caps, int compaction_level) const {
	if (type_ == transaction_type::ui_notification) {
		return {};
	}
	std::vector<message> messages;
	for (const auto& turn : turns_) {
		auto turn_msgs = turn->to_messages(caps, compaction_level);
		messages.insert(messages.end(), turn_msgs.begin(), turn_msgs.end());
	}
	return messages;
}

std::string Transaction::to_markdown() const {
	std::stringstream ss;
	for (size_t i = 0; i < turns_.size(); ++i) {
		if (i > 0) {
			ss << "\n---\n";
		}
		ss << turns_[i]->to_markdown();
	}
	return ss.str();
}

void Transaction::add_turn(std::shared_ptr<Turn> turn) {
	long long seq = turn->get_sequence_number();
	if (seq > 0) {
		if (min_turn_ == 0 || seq < min_turn_) {
			min_turn_ = seq;
		}
		if (max_turn_ == 0 || seq > max_turn_) {
			max_turn_ = seq;
		}
	}
	turns_.push_back(std::move(turn));
}

long long Transaction::get_min_turn() const {
	long long val = min_turn_;
	for (const auto& turn : turns_) {
		long long turn_seq = turn->get_sequence_number();
		if (turn_seq > 0) {
			if (val == 0 || turn_seq < val) {
				val = turn_seq;
			}
		}
	}
	return val;
}

long long Transaction::get_max_turn() const {
	long long val = max_turn_;
	for (const auto& turn : turns_) {
		long long turn_seq = turn->get_sequence_number();
		if (turn_seq > 0) {
			if (val == 0 || turn_seq > val) {
				val = turn_seq;
			}
		}
	}
	return val;
}

int Transaction::estimate_token_count(int compaction_level) const {
	int sum = 0;
	for (const auto& turn : turns_) {
		sum += turn->estimate_token_count(compaction_level);
	}
	return sum;
}

time_range Transaction::get_time_range() const {
	if (turns_.empty()) return {};
	time_range consolidated{ UINT64_MAX, 0 };
	for (const auto& turn : turns_) {
		time_range r = turn->get_time_range();
		consolidated.start_time = std::min(consolidated.start_time, r.start_time);
		consolidated.end_time = std::max(consolidated.end_time, r.end_time);
	}
	return consolidated;
}

nlohmann::json Transaction::serialize() const {
	nlohmann::json j;
	j["id"] = id_;
	j["type"] = static_cast<int>(type_);
	j["min_turn"] = get_min_turn();
	j["max_turn"] = get_max_turn();
	nlohmann::json turns_array = nlohmann::json::array();
	for (const auto& turn : turns_) {
		turns_array.push_back(turn->serialize());
	}
	j["turns"] = turns_array;
	return j;
}

std::shared_ptr<Transaction> Transaction::deserialize(const nlohmann::json& j) {
	std::string id = j.at("id").get<std::string>();
	transaction_type type = static_cast<transaction_type>(j.value("type", 0));
	auto tx = std::make_shared<Transaction>(id, type);
	tx->min_turn_ = j.value("min_turn", 0LL);
	tx->max_turn_ = j.value("max_turn", 0LL);
	if (j.contains("turns") && j["turns"].is_array()) {
		for (const auto& item : j["turns"]) {
			tx->add_turn(TurnRegistry::get_instance().deserialize(item));
		}
	}
	return tx;
}

} // namespace agentlib
