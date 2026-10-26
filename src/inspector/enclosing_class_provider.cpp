#include "inspector/enclosing_class_provider.h"
#include <format>
#include "fs_utils.h"
#include "inspector/class_summary_provider.h"
#include "utf8.h"

namespace turbostar
{

std::string enclosing_class_provider::make_cache_key(const inspector_query &q) const
{
	return std::format("{}:{}", q.file_path, q.line);
}

std::optional<tools::class_summary_info> enclosing_class_provider::lookup_cache(const std::string &key)
{
	std::lock_guard lock(cache_mutex_);
	auto it = cache_.find(key);
	if (it != cache_.end()) {
		return it->second;
	}
	return std::nullopt;
}

void enclosing_class_provider::store_cache(const std::string &key, const tools::class_summary_info &summary)
{
	std::lock_guard lock(cache_mutex_);
	cache_[key] = summary;
}

void enclosing_class_provider::clear_cache()
{
	std::lock_guard lock(cache_mutex_);
	cache_.clear();
}

inspector_result enclosing_class_provider::query(const inspector_query &q)
{
	inspector_result res;
	res.provider_name = "enclosing_class";

	if (q.file_path.empty()) {
		res.score = inspector_scores::INVALID;
		res.is_final = true;
		return res;
	}

	std::string key = make_cache_key(q);
	auto cached = lookup_cache(key);
	if (cached.has_value()) {
		bool is_member = false;
		if (!q.token.empty()) {
			for (const auto &f : cached->fields) {
				if (f.name == q.token) {
					is_member = true;
					break;
				}
			}
			if (!is_member) {
				for (const auto &m : cached->methods) {
					if (m.name == q.token) {
						is_member = true;
						break;
					}
				}
			}
		}
		res.score = is_member ? inspector_scores::CLASS_ENCLOSING_MEMBER : inspector_scores::CLASS_ENCLOSING;
		res.markdown = class_summary_provider::format_summary_markdown(*cached, q.available_lines, q.token);
		res.title_addon = std::format("class {} ({})", cached->class_name,
					      fs_utils::make_relative_to_project(cached->header_path));
		res.is_final = true;
		return res;
	}

	tools::class_summary_info summary;
	bool ok = tools::extract_enclosing_class_summary(q.file_path, q.line, summary);
	if (ok) {
		store_cache(key, summary);
		bool is_member = false;
		if (!q.token.empty()) {
			for (const auto &f : summary.fields) {
				if (f.name == q.token) {
					is_member = true;
					break;
				}
			}
			if (!is_member) {
				for (const auto &m : summary.methods) {
					if (m.name == q.token) {
						is_member = true;
						break;
					}
				}
			}
		}
		res.score = is_member ? inspector_scores::CLASS_ENCLOSING_MEMBER : inspector_scores::CLASS_ENCLOSING;
		res.markdown = class_summary_provider::format_summary_markdown(summary, q.available_lines, q.token);
		res.title_addon = std::format("class {} ({})", summary.class_name,
					      fs_utils::make_relative_to_project(summary.header_path));
		res.is_final = true;
		return res;
	}

	res.score = inspector_scores::INVALID;
	res.is_final = true;
	return res;
}

} // namespace turbostar
