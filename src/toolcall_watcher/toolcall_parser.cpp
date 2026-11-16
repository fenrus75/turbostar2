#include "toolcall_watcher/toolcall_parser.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace toolcall_watcher {

namespace {

constexpr size_t kMaxArgValueChars = 36;

std::string truncate_single_line(/* untrusted */ std::string_view raw, size_t max_chars)
{
	std::string out;
	out.reserve(std::min(raw.size(), max_chars + 3));

	bool prev_space = false;
	for (unsigned char ch : raw) {
		char clean = '\0';
		if (ch == '\n' || ch == '\r' || ch == '\t') {
			clean = ' ';
		} else if (ch >= 0x20 && ch != 0x7f) {
			clean = static_cast<char>(ch);
		} else {
			continue;
		}

		if (clean == ' ') {
			if (prev_space || out.empty()) {
				continue;
			}
			prev_space = true;
		} else {
			prev_space = false;
		}

		if (out.size() >= max_chars) {
			out += "...";
			return out;
		}
		out.push_back(clean);
	}

	while (!out.empty() && out.back() == ' ') {
		out.pop_back();
	}
	return out;
}

std::string format_json_value(const nlohmann::json &val)
{
	if (val.is_string()) {
		return std::format("\"{}\"", truncate_single_line(val.get<std::string>(), kMaxArgValueChars));
	}
	if (val.is_boolean()) {
		return val.get<bool>() ? "true" : "false";
	}
	if (val.is_number_integer()) {
		return std::format("{}", val.get<int64_t>());
	}
	if (val.is_number_unsigned()) {
		return std::format("{}", val.get<uint64_t>());
	}
	if (val.is_number_float()) {
		return std::format("{:g}", val.get<double>());
	}
	if (val.is_null()) {
		return "null";
	}
	if (val.is_array()) {
		std::string items;
		for (size_t i = 0; i < val.size(); ++i) {
			if (i > 0) {
				items += ", ";
			}
			items += format_json_value(val[i]);
			if (items.size() > kMaxArgValueChars) {
				items.resize(kMaxArgValueChars);
				items += "...";
				break;
			}
		}
		return std::format("[{}]", items);
	}
	return truncate_single_line(val.dump(), kMaxArgValueChars);
}

std::string_view trim_ascii(std::string_view sv) noexcept
{
	while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
		sv.remove_prefix(1);
	}
	while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
		sv.remove_suffix(1);
	}
	return sv;
}

} // namespace

std::vector<toolcall_entry> toolcall_parser::scan_directory(const std::filesystem::path &dir)
{
	std::vector<toolcall_entry> entries;
	std::error_code ec;
	if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
		return entries;
	}

	constexpr std::string_view kPrefix = "toolcall.";
	for (const auto &dirent : std::filesystem::directory_iterator(dir, ec)) {
		if (ec) {
			break;
		}
		if (!dirent.is_regular_file(ec)) {
			continue;
		}
		std::string fname = dirent.path().filename().string();
		if (!fname.starts_with(kPrefix) || fname.size() <= kPrefix.size()) {
			continue;
		}
		std::string_view suffix = std::string_view(fname).substr(kPrefix.size());
		uint64_t idx = 0;
		const char *begin = suffix.data();
		const char *end = begin + suffix.size();
		auto [ptr, parse_ec] = std::from_chars(begin, end, idx);
		if (parse_ec != std::errc{} || ptr != end) {
			continue;
		}
		entries.push_back(toolcall_entry{idx, dirent.path()});
	}

	std::sort(entries.begin(), entries.end(), [](const toolcall_entry &a, const toolcall_entry &b) {
		return a.index < b.index;
	});
	return entries;
}

std::optional<parsed_toolcall> toolcall_parser::parse_file(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	if (!in.is_open()) {
		return std::nullopt;
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	std::string untrusted_content = ss.str();
	return parse_content(untrusted_content);
}

std::optional<parsed_toolcall> toolcall_parser::parse_content(/* untrusted */ std::string_view content)
{
	// tool_tracer writes: <pretty json>\n---\n<output_result>
	size_t sep_pos = content.find("\n---\n");
	size_t sep_len = 5;
	if (sep_pos == std::string_view::npos) {
		sep_pos = content.find("\r\n---\r\n");
		sep_len = 7;
	}
	if (sep_pos == std::string_view::npos) {
		return std::nullopt;
	}

	std::string_view untrusted_json_sv = content.substr(0, sep_pos);
	std::string_view untrusted_body_sv = content.substr(sep_pos + sep_len);

	nlohmann::json j;
	try {
		j = nlohmann::json::parse(untrusted_json_sv);
	} catch (...) {
		return std::nullopt;
	}
	if (!j.is_object() || !j.contains("name") || !j["name"].is_string()) {
		return std::nullopt;
	}

	parsed_toolcall result;
	result.name = sanitize_for_terminal(j["name"].get<std::string>());
	if (j.contains("total_duration_us") && j["total_duration_us"].is_number_unsigned()) {
		result.total_duration_us = j["total_duration_us"].get<uint64_t>();
	}
	result.duration_label = format_duration(result.total_duration_us);

	std::string args_str;
	if (j.contains("arguments") && j["arguments"].is_object()) {
		bool first = true;
		for (auto it = j["arguments"].begin(); it != j["arguments"].end(); ++it) {
			if (!first) {
				args_str += ", ";
			}
			first = false;
			args_str += std::format("{}={}", sanitize_for_terminal(it.key()), format_json_value(it.value()));
		}
	} else if (j.contains("arguments") && !j["arguments"].is_null()) {
		args_str = format_json_value(j["arguments"]);
	}

	result.c_style_call = std::format("{}({})", result.name, args_str);
	result.body_markdown = sanitize_for_terminal(strip_result_wrapper(untrusted_body_sv));
	return result;
}

std::string toolcall_parser::strip_result_wrapper(/* untrusted */ std::string_view body)
{
	std::string_view trimmed = trim_ascii(body);
	if (!trimmed.starts_with('<')) {
		return std::string(body);
	}

	size_t first_nl = trimmed.find('\n');
	size_t last_nl = trimmed.rfind('\n');
	if (first_nl == std::string_view::npos || last_nl == std::string_view::npos || last_nl <= first_nl) {
		return std::string(body);
	}

	std::string_view first_line = trim_ascii(trimmed.substr(0, first_nl));
	std::string_view last_line = trim_ascii(trimmed.substr(last_nl + 1));

	if (first_line.size() < 3 || !first_line.starts_with('<') || !first_line.ends_with('>')) {
		return std::string(body);
	}

	std::string_view tag_name = first_line.substr(1, first_line.size() - 2);
	if (!tag_name.ends_with("_result")) {
		return std::string(body);
	}

	std::string expected_close = std::format("</{}>", tag_name);
	if (last_line != expected_close) {
		return std::string(body);
	}

	std::string_view inner = trimmed.substr(first_nl + 1, last_nl - first_nl - 1);
	return std::string(trim_ascii(inner));
}

std::string toolcall_parser::format_duration(uint64_t duration_us)
{
	if (duration_us < 1000) {
		return "<1ms";
	}
	if (duration_us < 1000000) {
		return std::format("{}ms", duration_us / 1000);
	}
	return std::format("{:.2f}s", static_cast<double>(duration_us) / 1000000.0);
}

std::string toolcall_parser::sanitize_for_terminal(/* untrusted */ std::string_view text)
{
	std::string out;
	out.reserve(text.size());
	for (unsigned char ch : text) {
		if (ch == '\r') {
			continue;
		}
		if (ch == '\n' || ch == '\t' || ch >= 0x20) {
			if (ch != 0x7f) {
				out.push_back(static_cast<char>(ch));
			}
		} else if (ch == 0x1b) {
			// Replace ESC to neutralize raw ANSI escape sequences.
			out.push_back('?');
		}
	}
	return out;
}

} // namespace toolcall_watcher
