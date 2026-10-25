#include "inspector/class_summary_provider.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <thread>
#include "fs_utils.h"
#include "inspector/inspector_helpers.h"
#include "utf8.h"

namespace turbostar
{

namespace
{

bool is_supported_cxx_file(const std::string &path)
{
	std::filesystem::path p(path);
	std::string ext = utf8::to_lower(p.extension().string());
	return ext == ".cpp" || ext == ".cc" || ext == ".c" || ext == ".cxx" ||
	       ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx";
}

} // namespace

std::string class_summary_provider::make_cache_key(const inspector_query &q) const
{
	return std::format("{}:{}:{}", q.file_path, q.line, q.token);
}

std::optional<cached_class_summary> class_summary_provider::lookup_cache(const std::string &key)
{
	std::lock_guard lock(cache_mutex_);
	auto it = cache_.find(key);
	if (it != cache_.end()) {
		return it->second;
	}
	return std::nullopt;
}

void class_summary_provider::store_cache(const std::string &key, const cached_class_summary &entry)
{
	std::lock_guard lock(cache_mutex_);
	cache_[key] = entry;
}

void class_summary_provider::clear_cache()
{
	std::lock_guard lock(cache_mutex_);
	cache_.clear();
}

std::string class_summary_provider::format_summary_markdown(const tools::class_summary_info &summary, int available_lines)
{
	int lines = std::max(1, available_lines);
	inspector::markdown_builder mb;

	auto append_fields_line = [&](inspector::markdown_builder &b, size_t max_items) {
		b.header("Fields:");
		size_t count = std::min(summary.fields.size(), max_items);
		for (size_t i = 0; i < count; ++i) {
			if (i > 0) {
				b.text(", ");
			}
			b.code(summary.fields[i].name);
		}
		if (summary.fields.size() > max_items) {
			b.text(" ...");
		}
		b.newline();
	};

	auto append_methods_line = [&](inspector::markdown_builder &b, std::string_view label, size_t start_idx, size_t count) {
		b.header(label);
		size_t end_idx = std::min(summary.methods.size(), start_idx + count);
		for (size_t i = start_idx; i < end_idx; ++i) {
			if (i > start_idx) {
				b.text(", ");
			}
			b.code(summary.methods[i].name + "()");
		}
		if (summary.methods.size() > end_idx) {
			b.text(" ...");
		}
		b.newline();
	};

	if (lines <= 2) {
		if (!summary.fields.empty() && !summary.methods.empty()) {
			append_fields_line(mb, 5);
			append_methods_line(mb, "Methods:", 0, 4);
		} else if (!summary.fields.empty()) {
			mb.header("Class:").code(summary.class_name).text(std::format(" ({} fields)", summary.fields.size())).newline();
			append_fields_line(mb, 8);
		} else {
			mb.header("Class:").code(summary.class_name).text(std::format(" ({} methods)", summary.methods.size())).newline();
			append_methods_line(mb, "Methods:", 0, 6);
		}
	} else if (lines == 3) {
		mb.header("Class:")
		    .code(summary.class_name)
		    .text(std::format(" ({} fields, {} methods)", summary.fields.size(), summary.methods.size()))
		    .newline();

		if (!summary.fields.empty()) {
			append_fields_line(mb, 6);
		} else {
			append_methods_line(mb, "Methods:", 0, 4);
		}

		if (!summary.methods.empty()) {
			if (!summary.fields.empty()) {
				append_methods_line(mb, "Methods:", 0, 5);
			} else {
				append_methods_line(mb, "More:", 4, 5);
			}
		}
	} else { // lines >= 4
		mb.header("Class:")
		    .code(summary.class_name)
		    .text(std::format(" ({} fields, {} methods)", summary.fields.size(), summary.methods.size()))
		    .newline();

		if (!summary.fields.empty()) {
			append_fields_line(mb, 6);
		}

		if (!summary.methods.empty()) {
			append_methods_line(mb, "Methods:", 0, 4);
			if (summary.methods.size() > 4) {
				append_methods_line(mb, "More:", 4, 5);
			}
		}
	}

	return mb.build();
}

inspector_result class_summary_provider::query(const inspector_query &q)
{
	inspector_result res;
	res.provider_name = "class_summary";

	if (q.file_path.empty() || !is_supported_cxx_file(q.file_path)) {
		res.score = inspector_scores::INVALID;
		res.is_final = true;
		return res;
	}

	std::string key = make_cache_key(q);
	auto cached = lookup_cache(key);
	if (cached.has_value()) {
		if (cached->exists) {
			res.score = inspector_scores::CLASS_SUMMARY;
			res.markdown = format_summary_markdown(cached->summary, q.available_lines);
			res.title_addon = std::format("class {} ({})", cached->summary.class_name,
						      fs_utils::make_relative_to_project(cached->summary.header_path));
			res.is_final = true;
			return res;
		}
		res.score = inspector_scores::INVALID;
		res.is_final = true;
		return res;
	}

	res.score = inspector_scores::RESOLVING_HINT;
	res.markdown = std::format("`{}` *[inspecting class...]*", q.token.empty() ? "class" : q.token);
	res.is_final = false;
	return res;
}

void class_summary_provider::query_async(const inspector_query &q,
					 std::function<void(inspector_result)> callback)
{
	if (q.file_path.empty() || !is_supported_cxx_file(q.file_path)) {
		if (callback) {
			inspector_result res;
			res.provider_name = "class_summary";
			res.score = inspector_scores::INVALID;
			res.is_final = true;
			callback(res);
		}
		return;
	}

	std::string key = make_cache_key(q);
	auto cached = lookup_cache(key);
	if (cached.has_value()) {
		if (callback) {
			inspector_result res;
			res.provider_name = "class_summary";
			if (cached->exists) {
				res.score = inspector_scores::CLASS_SUMMARY;
				res.markdown = format_summary_markdown(cached->summary, q.available_lines);
				res.title_addon = std::format("class {} ({})", cached->summary.class_name,
							      fs_utils::make_relative_to_project(cached->summary.header_path));
			} else {
				res.score = inspector_scores::INVALID;
			}
			res.is_final = true;
			callback(res);
		}
		return;
	}

	std::thread([this, q, key, callback]() {
		tools::class_summary_info summary;
		bool ok = tools::extract_class_summary(q.file_path, q.line, q.col, q.token, summary);

		cached_class_summary entry;
		entry.exists = ok;
		if (ok) {
			entry.summary = summary;
		}
		store_cache(key, entry);

		if (callback) {
			inspector_result res;
			res.provider_name = "class_summary";
			if (ok) {
				res.score = inspector_scores::CLASS_SUMMARY;
				res.markdown = format_summary_markdown(summary, q.available_lines);
				res.title_addon = std::format("class {} ({})", summary.class_name,
							      fs_utils::make_relative_to_project(summary.header_path));
			} else {
				res.score = inspector_scores::INVALID;
			}
			res.is_final = true;
			callback(res);
		}
	}).detach();
}

} // namespace turbostar
