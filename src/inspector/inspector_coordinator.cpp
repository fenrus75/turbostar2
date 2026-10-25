#include "inspector/inspector_coordinator.h"

void inspector_coordinator::register_provider(std::unique_ptr<inspector_provider> provider)
{
	std::lock_guard<std::mutex> lock(mutex_);
	providers_.push_back(std::move(provider));
}

inspector_result inspector_coordinator::query(const inspector_query &q,
					      std::function<void(inspector_result)> async_callback)
{
	std::vector<inspector_provider *> prov_ptrs;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		prov_ptrs.reserve(providers_.size());
		for (const auto &p : providers_) {
			prov_ptrs.push_back(p.get());
		}
	}

	inspector_result highest_result;
	highest_result.score = inspector_scores::INVALID;

	for (auto *p : prov_ptrs) {
		auto res = p->query(q);
		// Score of INVALID (0 or lower) means: do not use this result under any circumstances
		if (res.score > highest_result.score && res.score > inspector_scores::INVALID) {
			highest_result = res;
		}

		if (!res.is_final && async_callback) {
			p->query_async(q, [async_callback](inspector_result async_res) {
				if (async_res.score > inspector_scores::INVALID) {
					async_callback(async_res);
				}
			});
		}
	}

	if (highest_result.score <= inspector_scores::INVALID) {
		highest_result.score = inspector_scores::INVALID;
		highest_result.provider_name = "none";
		highest_result.markdown = "*[No symbol under cursor]*";
		highest_result.is_final = true;
	}

	return highest_result;
}
