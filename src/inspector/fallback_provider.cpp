#include "inspector/fallback_provider.h"
#include <format>
#include "inspector/inspector_helpers.h"

inspector_result fallback_inspector_provider::query(const inspector_query &q)
{
	inspector_result res;
	res.provider_name = "token";
	res.is_final = true;

	if (q.token.empty()) {
		res.score = inspector_scores::INVALID;
		res.markdown = "*[No symbol under cursor]*";
	} else {
		res.score = inspector_scores::FALLBACK_TOKEN;
		res.markdown = std::format("`{}`", q.token);
	}
	return res;
}
