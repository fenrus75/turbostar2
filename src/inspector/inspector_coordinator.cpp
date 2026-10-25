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
	highest_result.score = -1;

	for (auto *p : prov_ptrs) {
		auto res = p->query(q);
		if (res.score > highest_result.score) {
			highest_result = res;
		}

		if (!res.is_final && async_callback) {
			p->query_async(q, [async_callback](inspector_result async_res) {
				async_callback(async_res);
			});
		}
	}

	if (highest_result.score < 0) {
		highest_result.score = inspector_scores::NONE;
		highest_result.provider_name = "none";
		highest_result.markdown = "*[No symbol under cursor]*";
		highest_result.is_final = true;
	}

	return highest_result;
}
