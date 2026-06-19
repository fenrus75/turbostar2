#pragma once

#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "transaction.h"

#define COMPACTION_LEVEL_RAW 0
#define COMPACTION_LEVEL_STRIP_REASONING 1
#define COMPACTION_LEVEL_STRIP_TOOL_CALLS 2
#define COMPACTION_LEVEL_PAGED_OUT 99

namespace agentlib {

class Episode {
public:
	Episode(std::string id, std::string title, std::string summary);

	std::string get_id() const { return id_; }
	std::string get_title() const { return title_; }
	std::string get_summary() const { return summary_; }

	void set_title(std::string title) { title_ = std::move(title); }
	void set_summary(std::string summary) { summary_ = std::move(summary); }

	int get_compaction_level() const { return compaction_level_; }
	void set_compaction_level(int level) { compaction_level_ = level; }

	bool is_finalized() const { return finalized_; }
	void set_finalized(bool finalized) { finalized_ = finalized; }

	long long get_sequence_number() const { return sequence_number_; }
	void set_sequence_number(long long seq) {
		if (sequence_number_ == 0) {
			sequence_number_ = seq;
		}
	}

	void copy_from(const Episode& other);
	void clear_transactions();

	std::string get_reactivation_hint() const { return reactivation_hint_; }
	void set_reactivation_hint(std::string hint) { reactivation_hint_ = std::move(hint); }

	void add_transaction(std::shared_ptr<Transaction> tx);
	void insert_transaction(size_t index, std::shared_ptr<Transaction> tx);
	const std::vector<std::shared_ptr<Transaction>>& get_transactions() const { return transactions_; }

	long long get_min_turn() const;
	long long get_max_turn() const;
	void set_min_turn(long long val) { min_turn_ = val; }
	void set_max_turn(long long val) { max_turn_ = val; }

	std::vector<message> to_messages(const model_capabilities& caps, bool include_anchor = true) const;

	int estimate_token_count(int compaction_level) const;
	time_range get_time_range() const;

	nlohmann::json serialize() const;
	static std::shared_ptr<Episode> deserialize(const nlohmann::json& j);

private:
	std::string id_;
	std::string title_;
	std::string summary_;
	int compaction_level_{0};
	bool finalized_{false};
	long long sequence_number_{0};
	std::string reactivation_hint_;
	std::vector<std::shared_ptr<Transaction>> transactions_;
	long long min_turn_{0};
	long long max_turn_{0};
};

} // namespace agentlib
