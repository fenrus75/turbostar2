#pragma once

#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "turn.h"

namespace agentlib {

enum class transaction_type {
	user_exchange,
	system_injection,
	subagent_lifecycle,
	ui_notification
};

class Transaction {
public:
	Transaction(std::string id, transaction_type type);

	std::string get_id() const { return id_; }
	transaction_type get_type() const { return type_; }

	std::vector<message> to_messages(const model_capabilities& caps, int compaction_level) const;
	std::string to_markdown() const;

	void add_turn(std::shared_ptr<Turn> turn);
	const std::vector<std::shared_ptr<Turn>>& get_turns() const { return turns_; }

	long long get_min_turn() const;
	long long get_max_turn() const;
	void set_min_turn(long long val) { min_turn_ = val; }
	void set_max_turn(long long val) { max_turn_ = val; }

	int estimate_token_count(int compaction_level) const;
	time_range get_time_range() const;

	nlohmann::json serialize() const;
	static std::shared_ptr<Transaction> deserialize(const nlohmann::json& j);

private:
	std::string id_;
	transaction_type type_;
	std::vector<std::shared_ptr<Turn>> turns_;
	long long min_turn_{0};
	long long max_turn_{0};
};

} // namespace agentlib
