#include "turn.h"

namespace agentlib {

void Turn::append_content(const std::string& chunk) {
	content_ += chunk;
	if (interaction_) {
		interaction_->push_incremental_content(chunk);
	}
}

} // namespace agentlib
