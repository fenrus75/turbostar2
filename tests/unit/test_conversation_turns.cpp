#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "../../src/agentlib/data/conversation.h"
#include "../../src/agentlib/data/system_turn.h"
#include "../../src/agentlib/data/user_turn.h"
#include "../../src/agentlib/data/model_response_turn.h"
#include "../../src/agentlib/data/transaction.h"
#include "../../src/agentlib/data/episode.h"

using namespace agentlib;

int main() {
	test_watchdog::setup_watchdog(30);

	// 1. Create a conversation
	auto convo = std::make_shared<Conversation>();
	assert(convo->get_next_turn_seq() == 1);

	// 2. Create active episode
	auto ep = convo->create_new_episode("ep_1", "Test Episode", "Testing sequence numbers");
	assert(ep->get_min_turn() == 0);
	assert(ep->get_max_turn() == 0);

	// 3. Create a transaction
	auto tx = std::make_shared<Transaction>("tx_1", transaction_type::user_exchange);
	assert(tx->get_min_turn() == 0);
	assert(tx->get_max_turn() == 0);

	// 4. Add turn 1 (system turn)
	auto turn1 = std::make_shared<system_turn>("turn_1", "Hello system", "base");
	turn1->set_sequence_number(convo->allocate_next_turn_seq());
	assert(turn1->get_sequence_number() == 1);
	tx->add_turn(turn1);

	assert(tx->get_min_turn() == 1);
	assert(tx->get_max_turn() == 1);

	// 5. Add transaction to episode/conversation
	convo->add_transaction(tx);
	assert(ep->get_min_turn() == 1);
	assert(ep->get_max_turn() == 1);

	// 6. Create a second transaction and add turn 2 (user turn)
	auto tx2 = std::make_shared<Transaction>("tx_2", transaction_type::user_exchange);
	auto turn2 = std::make_shared<user_turn>("turn_2", "Hello user");
	turn2->set_sequence_number(convo->allocate_next_turn_seq());
	assert(turn2->get_sequence_number() == 2);
	tx2->add_turn(turn2);
	convo->add_transaction(tx2);

	assert(ep->get_min_turn() == 1);
	assert(ep->get_max_turn() == 2);
	assert(convo->get_next_turn_seq() == 3);

	// 7. Verify get_turns_since
	auto turns_since_0 = convo->get_turns_since(0);
	assert(turns_since_0.size() == 2);
	assert(turns_since_0[0]->get_id() == "turn_1");
	assert(turns_since_0[1]->get_id() == "turn_2");

	auto turns_since_1 = convo->get_turns_since(1);
	assert(turns_since_1.size() == 1);
	assert(turns_since_1[0]->get_id() == "turn_2");

	auto turns_since_2 = convo->get_turns_since(2);
	assert(turns_since_2.empty());

	// 8. Test serialization and deserialization
	auto j = convo->serialize();
	assert(j.contains("next_turn_seq"));
	assert(j["next_turn_seq"] == 3);

	auto convo2 = Conversation::deserialize(j);
	assert(convo2->get_next_turn_seq() == 3);
	
	auto ep2 = convo2->get_current_episode();
	assert(ep2 != nullptr);
	assert(ep2->get_min_turn() == 1);
	assert(ep2->get_max_turn() == 2);

	auto turns_since_0_convo2 = convo2->get_turns_since(0);
	assert(turns_since_0_convo2.size() == 2);
	assert(turns_since_0_convo2[0]->get_sequence_number() == 1);
	assert(turns_since_0_convo2[0]->get_id() == "turn_1");
	assert(turns_since_0_convo2[1]->get_sequence_number() == 2);
	assert(turns_since_0_convo2[1]->get_id() == "turn_2");

	std::cout << "test_conversation_turns unit tests passed!\n";
	return 0;
}
