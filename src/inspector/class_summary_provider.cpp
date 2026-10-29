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

std::string extract_member_declaration(const tools::class_summary_info &summary,
				       const tools::class_member_info &member,
				       bool is_field)
{
	if (summary.header_lines.empty() || member.start_line <= 0) {
		return "";
	}
	int start_idx = member.start_line - 1;
	int max_lines = static_cast<int>(summary.header_lines.size());
	if (start_idx >= max_lines) {
		return "";
	}
	int end_idx = member.end_line > 0 ? (member.end_line - 1) : start_idx;
	end_idx = std::clamp(end_idx, start_idx, std::min(max_lines - 1, start_idx + 4));

	std::string decl;
	for (int i = start_idx; i <= end_idx; ++i) {
		std::string_view l = summary.header_lines[i];

		// Strip line comments
		size_t c_pos = l.find("//");
		if (c_pos != std::string_view::npos) {
			l = l.substr(0, c_pos);
		}
		size_t b_pos = l.find("/*");
		if (b_pos != std::string_view::npos) {
			l = l.substr(0, b_pos);
		}

		// Trim whitespace
		size_t first = l.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos) {
			continue;
		}
		size_t last = l.find_last_not_of(" \t\r\n");
		l = l.substr(first, last - first + 1);

		// Strip access specifiers if on same line (e.g. "public: int x;")
		if (l.starts_with("public:") || l.starts_with("protected:") || l.starts_with("private:")) {
			size_t colon = l.find(':');
			l.remove_prefix(colon + 1);
			size_t next_non_ws = l.find_first_not_of(" \t");
			if (next_non_ws == std::string_view::npos) {
				continue;
			}
			l.remove_prefix(next_non_ws);
		}

		if (!decl.empty()) {
			decl += " ";
		}
		decl += l;

		if (is_field) {
			if (decl.find(';') != std::string::npos) {
				break;
			}
		} else {
			// For method: if we have seen ')' and then '{' or ';', we're done
			size_t rparen = decl.find(')');
			if (rparen != std::string_view::npos) {
				if (decl.find(';', rparen) != std::string::npos ||
				    decl.find('{', rparen) != std::string::npos) {
					break;
				}
			}
		}
	}

	if (is_field) {
		size_t sc = decl.find(';');
		if (sc != std::string::npos) {
			decl = decl.substr(0, sc + 1);
		}
	} else {
		size_t rparen = decl.find(')');
		if (rparen != std::string_view::npos) {
			size_t brace = decl.find('{', rparen);
			if (brace != std::string_view::npos) {
				decl = decl.substr(0, brace);
				size_t last_ws = decl.find_last_not_of(" \t");
				if (last_ws != std::string_view::npos) {
					decl = decl.substr(0, last_ws + 1);
				}
				if (!decl.ends_with(';')) {
					decl += ";";
				}
			} else {
				size_t sc = decl.find(';', rparen);
				if (sc != std::string_view::npos) {
					decl = decl.substr(0, sc + 1);
				}
			}
		}
	}

	// Collapse multiple spaces/tabs into a single space
	std::string collapsed;
	collapsed.reserve(decl.size());
	bool in_space = false;
	for (char c : decl) {
		if (c == ' ' || c == '\t') {
			if (!in_space) {
				collapsed.push_back(' ');
				in_space = true;
			}
		} else {
			collapsed.push_back(c);
			in_space = false;
		}
	}
	size_t f = collapsed.find_first_not_of(' ');
	if (f == std::string::npos) {
		return "";
	}
	size_t l = collapsed.find_last_not_of(' ');
	return collapsed.substr(f, l - f + 1);
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

std::string class_summary_provider::format_summary_markdown(const tools::class_summary_info &summary,
							    int available_lines,
							    int available_cols,
							    std::string_view focused_token)
{
	int lines = std::max(1, available_lines);
	int cols = std::max(20, available_cols);
	inspector::markdown_builder mb;

	const tools::class_member_info *focused_field = nullptr;
	const tools::class_member_info *focused_method = nullptr;

	if (!focused_token.empty()) {
		for (const auto &f : summary.fields) {
			if (f.name == focused_token) {
				focused_field = &f;
				break;
			}
		}
		if (!focused_field) {
			for (const auto &m : summary.methods) {
				if (m.name == focused_token) {
					focused_method = &m;
					break;
				}
			}
		}
	}

	auto append_fields_line = [&](inspector::markdown_builder &b, std::string_view label, size_t start_idx) -> size_t {
		if (start_idx >= summary.fields.size()) {
			return start_idx;
		}
		b.header(label);
		int rem_cols = cols - (static_cast<int>(label.size()) + 1);
		size_t i = start_idx;
		bool added_any = false;
		for (; i < summary.fields.size(); ++i) {
			const auto &name = summary.fields[i].name;
			int cost = (added_any ? 2 : 0) + static_cast<int>(name.size());
			bool has_more = (i + 1 < summary.fields.size());
			int reserve = has_more ? 4 : 0;
			if (added_any && cost + reserve > rem_cols) {
				break;
			}
			if (added_any) {
				b.text(", ");
			}
			b.code(name);
			rem_cols -= cost;
			added_any = true;
		}
		if (i < summary.fields.size()) {
			b.text(" ...");
		}
		b.newline();
		return i;
	};

	auto append_methods_line = [&](inspector::markdown_builder &b, std::string_view label, size_t start_idx) -> size_t {
		if (start_idx >= summary.methods.size()) {
			return start_idx;
		}
		b.header(label);
		int rem_cols = cols - (static_cast<int>(label.size()) + 1);
		size_t i = start_idx;
		bool added_any = false;
		for (; i < summary.methods.size(); ++i) {
			const auto &name = summary.methods[i].name;
			int cost = (added_any ? 2 : 0) + static_cast<int>(name.size()) + 2; // +2 for "()"
			bool has_more = (i + 1 < summary.methods.size());
			int reserve = has_more ? 4 : 0;
			if (added_any && cost + reserve > rem_cols) {
				break;
			}
			if (added_any) {
				b.text(", ");
			}
			b.code(name + "()");
			rem_cols -= cost;
			added_any = true;
		}
		if (i < summary.methods.size()) {
			b.text(" ...");
		}
		b.newline();
		return i;
	};

	auto append_focused_member = [&](inspector::markdown_builder &b) {
		if (focused_field) {
			std::string decl = extract_member_declaration(summary, *focused_field, true);
			b.header("Field:").code(decl.empty() ? focused_field->name : decl).newline();
		} else if (focused_method) {
			std::string decl = extract_member_declaration(summary, *focused_method, false);
			b.header("Method:").code(decl.empty() ? (focused_method->name + "()") : decl).newline();
		}
	};

	bool has_focused = (focused_field != nullptr || focused_method != nullptr);
	size_t next_field_idx = 0;
	size_t next_method_idx = 0;

	if (lines == 1) {
		if (has_focused) {
			append_focused_member(mb);
		} else if (!summary.fields.empty()) {
			append_fields_line(mb, "Fields:", 0);
		} else if (!summary.methods.empty()) {
			append_methods_line(mb, "Methods:", 0);
		}
	} else if (lines == 2) {
		if (has_focused) {
			if (focused_field) {
				if (!summary.fields.empty()) {
					append_fields_line(mb, "Fields:", 0);
				} else {
					append_methods_line(mb, "Methods:", 0);
				}
			} else {
				if (!summary.methods.empty()) {
					append_methods_line(mb, "Methods:", 0);
				} else {
					append_fields_line(mb, "Fields:", 0);
				}
			}
			append_focused_member(mb);
		} else {
			if (!summary.fields.empty() && !summary.methods.empty()) {
				append_fields_line(mb, "Fields:", 0);
				append_methods_line(mb, "Methods:", 0);
			} else if (!summary.fields.empty()) {
				next_field_idx = append_fields_line(mb, "Fields:", 0);
				if (next_field_idx < summary.fields.size()) {
					append_fields_line(mb, "More:", next_field_idx);
				}
			} else if (!summary.methods.empty()) {
				next_method_idx = append_methods_line(mb, "Methods:", 0);
				if (next_method_idx < summary.methods.size()) {
					append_methods_line(mb, "More:", next_method_idx);
				}
			}
		}
	} else if (lines == 3) {
		if (has_focused) {
			if (!summary.fields.empty()) {
				next_field_idx = append_fields_line(mb, "Fields:", 0);
			}
			if (!summary.methods.empty()) {
				append_methods_line(mb, "Methods:", 0);
			} else if (next_field_idx < summary.fields.size()) {
				append_fields_line(mb, "More:", next_field_idx);
			}
			append_focused_member(mb);
		} else {
			if (!summary.fields.empty()) {
				next_field_idx = append_fields_line(mb, "Fields:", 0);
			}
			if (!summary.methods.empty()) {
				next_method_idx = append_methods_line(mb, "Methods:", 0);
				if (next_method_idx < summary.methods.size()) {
					append_methods_line(mb, "More:", next_method_idx);
				} else if (next_field_idx < summary.fields.size()) {
					append_fields_line(mb, "More:", next_field_idx);
				}
			} else if (next_field_idx < summary.fields.size()) {
				next_field_idx = append_fields_line(mb, "More:", next_field_idx);
				if (next_field_idx < summary.fields.size()) {
					append_fields_line(mb, "More:", next_field_idx);
				}
			}
		}
	} else { // lines >= 4
		if (has_focused) {
			if (!summary.fields.empty()) {
				next_field_idx = append_fields_line(mb, "Fields:", 0);
			}
			if (!summary.methods.empty()) {
				next_method_idx = append_methods_line(mb, "Methods:", 0);
			}
			append_focused_member(mb);
			if (next_method_idx < summary.methods.size()) {
				append_methods_line(mb, "More:", next_method_idx);
			} else if (next_field_idx < summary.fields.size()) {
				append_fields_line(mb, "More:", next_field_idx);
			}
		} else {
			if (!summary.fields.empty()) {
				next_field_idx = append_fields_line(mb, "Fields:", 0);
			}
			if (!summary.methods.empty()) {
				next_method_idx = append_methods_line(mb, "Methods:", 0);
				if (next_method_idx < summary.methods.size()) {
					next_method_idx = append_methods_line(mb, "More:", next_method_idx);
				}
				if (next_method_idx < summary.methods.size()) {
					append_methods_line(mb, "More:", next_method_idx);
				} else if (next_field_idx < summary.fields.size()) {
					append_fields_line(mb, "More:", next_field_idx);
				}
			} else if (next_field_idx < summary.fields.size()) {
				next_field_idx = append_fields_line(mb, "More:", next_field_idx);
				if (next_field_idx < summary.fields.size()) {
					append_fields_line(mb, "More:", next_field_idx);
				}
			}
		}
	}

	return mb.build();
}

inspector_result class_summary_provider::query(const inspector_query &q)
{
	inspector_result res;
	res.provider_name = "class_summary";

	if (q.file_path.empty() || q.token.empty() || !is_supported_cxx_file(q.file_path)) {
		res.score = inspector_scores::INVALID;
		res.is_final = true;
		return res;
	}

	std::string key = make_cache_key(q);
	auto cached = lookup_cache(key);
	if (cached.has_value()) {
		if (cached->exists) {
			res.score = inspector_scores::CLASS_TOKEN;
			res.markdown = format_summary_markdown(cached->summary, q.available_lines, q.available_cols, q.token);
			res.title_addon = std::format("class {} ({})", cached->summary.class_name,
						      fs_utils::make_relative_to_project(cached->summary.header_path));
			res.is_final = true;
			return res;
		}
		res.score = inspector_scores::INVALID;
		res.is_final = true;
		return res;
	}

	res.score = inspector_scores::INVALID;
	res.is_final = false;
	return res;
}

void class_summary_provider::query_async(const inspector_query &q,
					 std::function<void(inspector_result)> callback)
{
	if (q.file_path.empty() || q.token.empty() || !is_supported_cxx_file(q.file_path)) {
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
				res.score = inspector_scores::CLASS_TOKEN;
				res.markdown = format_summary_markdown(cached->summary, q.available_lines, q.available_cols, q.token);
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
		bool ok = tools::extract_token_class_summary(q.file_path, q.line, q.col, q.token, summary);

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
				res.score = inspector_scores::CLASS_TOKEN;
				res.markdown = format_summary_markdown(summary, q.available_lines, q.available_cols, q.token);
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
