#include "tool_validator.h"
#include "../codereview_manager.h"
#include <algorithm>

namespace agentlib {

static std::vector<std::string> split_families(const std::string &s) {
	std::vector<std::string> res;
	size_t start = 0, end;
	while ((end = s.find('|', start)) != std::string::npos) {
		res.push_back(s.substr(start, end - start));
		start = end + 1;
	}
	res.push_back(s.substr(start));
	return res;
}

bool tool_validator::is_allowed_for_agent(const agent_properties &properties) const {
	std::string family_str = get_family();
	for (const auto &fam : split_families(family_str)) {
		if (fam == "base") {
			return true;
		}
		if (std::find(properties.active_families.begin(), properties.active_families.end(), fam) != properties.active_families.end()) {
			return true;
		}
		if (fam == "code_review" && codereview_manager::get_instance().has_active_items()) {
			return true;
		}
	}
	return false;
}

} // namespace agentlib
