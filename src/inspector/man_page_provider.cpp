#include "inspector/man_page_provider.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>
#include <zlib.h>
#include "inspector/inspector_helpers.h"
#include "tools/troff2md.h"

man_page_provider::man_page_provider(std::string man3_dir)
    : man3_dir_(std::move(man3_dir))
{
}

void man_page_provider::clear_cache()
{
	std::lock_guard<std::mutex> lock(cache_mutex_);
	cache_.clear();
}

size_t man_page_provider::cache_size() const
{
	std::lock_guard<std::mutex> lock(cache_mutex_);
	return cache_.size();
}

std::optional<cached_man_entry> man_page_provider::lookup_cached(const std::string &token) const
{
	std::lock_guard<std::mutex> lock(cache_mutex_);
	auto it = cache_.find(token);
	if (it != cache_.end()) {
		return it->second;
	}
	return std::nullopt;
}

void man_page_provider::store_cached(const std::string &token, const cached_man_entry &entry)
{
	std::lock_guard<std::mutex> lock(cache_mutex_);
	if (cache_.size() >= 512) {
		cache_.clear(); // Simple bounded cache reset
	}
	cache_[token] = entry;
}

std::string man_page_provider::find_man_file(const std::string &token) const
{
	if (token.empty()) {
		return "";
	}

	std::filesystem::path base(man3_dir_);
	std::error_code ec;
	if (!std::filesystem::is_directory(base, ec) || ec) {
		return "";
	}

	const std::vector<std::string> suffixes = {
		".3.gz",
		".3",
		".3type.gz",
		".3type",
		".3head.gz",
		".3head"
	};

	for (const auto &suffix : suffixes) {
		auto p = base / (token + suffix);
		if (std::filesystem::exists(p, ec) && !ec) {
			return p.string();
		}
	}
	return "";
}

static std::string read_file_or_gzip(const std::filesystem::path &path)
{
	constexpr size_t max_bytes = 128 * 1024; // 128KB cap for man page header/synopsis
	if (path.extension() == ".gz") {
		gzFile file = gzopen(path.c_str(), "rb");
		if (!file) {
			return "";
		}
		std::string content;
		char buf[4096];
		int bytes = 0;
		while ((bytes = gzread(file, buf, sizeof(buf))) > 0) {
			content.append(buf, bytes);
			if (content.size() >= max_bytes) {
				break;
			}
		}
		gzclose(file);
		return content;
	}

	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return "";
	}
	std::string content;
	char buf[4096];
	while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
		content.append(buf, in.gcount());
		if (content.size() >= max_bytes) {
			break;
		}
	}
	return content;
}

std::string man_page_provider::collapse_prototype(std::string_view raw_proto)
{
	if (raw_proto.empty()) {
		return "";
	}

	std::string combined;
	size_t start = 0;
	while (start < raw_proto.size()) {
		size_t end = raw_proto.find('\n', start);
		if (end == std::string_view::npos) {
			end = raw_proto.size();
		}

		std::string_view line = raw_proto.substr(start, end - start);
		// Trim leading spaces
		while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
			line.remove_prefix(1);
		}
		// Trim trailing spaces
		while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
			line.remove_suffix(1);
		}

		if (!line.empty()) {
			if (!combined.empty()) {
				combined += ' ';
			}
			combined += line;
		}
		start = end + 1;
	}

	// Collapse multiple spaces into one, clean up spacing around punctuation
	std::string collapsed;
	bool in_space = false;
	for (char c : combined) {
		if (c == ' ' || c == '\t' || c == '\n') {
			if (!in_space && !collapsed.empty()) {
				collapsed += ' ';
				in_space = true;
			}
		} else {
			if (in_space && (c == '(' || c == ',' || c == ';' || c == ')' || c == ']')) {
				if (!collapsed.empty() && collapsed.back() == ' ') {
					collapsed.pop_back();
				}
			}
			collapsed += c;
			in_space = false;
		}
	}

	// Remove unwanted space right after '(' or '['
	std::string cleaned;
	for (size_t i = 0; i < collapsed.size(); ++i) {
		if ((collapsed[i] == '(' || collapsed[i] == '[') && i + 1 < collapsed.size() && collapsed[i + 1] == ' ') {
			cleaned += collapsed[i];
			i++; // Skip following space
		} else {
			cleaned += collapsed[i];
		}
	}

	// Strip surrounding backticks if any
	while (!cleaned.empty() && cleaned.front() == '`') {
		cleaned.erase(cleaned.begin());
	}
	while (!cleaned.empty() && cleaned.back() == '`') {
		cleaned.pop_back();
	}

	return cleaned;
}

std::string man_page_provider::extract_synopsis_prototype(std::string_view full_md, std::string_view token)
{
	if (full_md.empty() || token.empty()) {
		return "";
	}

	// 1. Locate SYNOPSIS section
	size_t syn_pos = full_md.find("SYNOPSIS");
	if (syn_pos == std::string_view::npos) {
		// Try lowercase
		syn_pos = full_md.find("Synopsis");
		if (syn_pos == std::string_view::npos) {
			syn_pos = full_md.find("synopsis");
		}
	}
	if (syn_pos == std::string_view::npos) {
		return "";
	}

	// Advance past heading line
	size_t line_end = full_md.find('\n', syn_pos);
	if (line_end == std::string_view::npos) {
		return "";
	}
	std::string_view syn_block = full_md.substr(line_end + 1);

	// Find the end of SYNOPSIS (next section header '# ')
	size_t next_sec = syn_block.find("\n# ");
	if (next_sec != std::string_view::npos) {
		syn_block = syn_block.substr(0, next_sec);
	}

	// Split synopsis block into lines
	std::vector<std::string_view> lines;
	size_t pos = 0;
	while (pos < syn_block.size()) {
		size_t nl = syn_block.find('\n', pos);
		if (nl == std::string_view::npos) {
			lines.push_back(syn_block.substr(pos));
			break;
		}
		lines.push_back(syn_block.substr(pos, nl - pos));
		pos = nl + 1;
	}

	// 2. Search for the line containing token as function call (token followed by '(')
	int match_idx = -1;
	for (size_t i = 0; i < lines.size(); ++i) {
		std::string_view l = lines[i];
		// Skip #include lines
		if (l.find("#include") != std::string_view::npos) {
			continue;
		}

		size_t t_pos = l.find(token);
		while (t_pos != std::string_view::npos) {
			bool boundary_before = (t_pos == 0 ||
			                        (!std::isalnum(static_cast<unsigned char>(l[t_pos - 1])) &&
			                         l[t_pos - 1] != '_'));
			size_t after = t_pos + token.size();
			while (after < l.size() && (l[after] == ' ' || l[after] == '*' || l[after] == '`')) {
				after++;
			}
			bool is_call = (after < l.size() && l[after] == '(');

			if (boundary_before && is_call) {
				match_idx = static_cast<int>(i);
				break;
			}
			t_pos = l.find(token, t_pos + 1);
		}
		if (match_idx >= 0) {
			break;
		}
	}

	if (match_idx < 0) {
		return "";
	}

	// Check if the preceding line holds the return type
	int start_idx = match_idx;
	if (match_idx > 0) {
		std::string_view prev = lines[match_idx - 1];
		while (!prev.empty() && (prev.front() == ' ' || prev.front() == '\t')) {
			prev.remove_prefix(1);
		}
		if (!prev.empty() && prev.find("#include") == std::string_view::npos &&
		    prev.back() != ';' && prev.back() != '{' && prev.back() != '>') {
			start_idx = match_idx - 1;
		}
	}

	// Collect lines until semicolon is encountered
	std::string collected;
	for (size_t k = static_cast<size_t>(start_idx); k < lines.size(); ++k) {
		std::string_view l = lines[k];
		if (!collected.empty()) {
			collected += '\n';
		}
		collected += l;
		if (l.find(';') != std::string_view::npos) {
			break;
		}
		// Safety stop if we hit an empty line or another function
		if (k > static_cast<size_t>(match_idx) && l.empty()) {
			break;
		}
	}

	return collapse_prototype(collected);
}

std::string man_page_provider::extract_short_description(std::string_view full_md, std::string_view token)
{
	(void)token;
	if (full_md.empty()) {
		return "";
	}

	// 1. Try extracting from # NAME section
	size_t name_pos = full_md.find("NAME");
	if (name_pos != std::string_view::npos) {
		size_t line_end = full_md.find('\n', name_pos);
		if (line_end != std::string_view::npos) {
			size_t next_sec = full_md.find("\n# ", line_end);
			std::string_view name_block = full_md.substr(line_end + 1, next_sec == std::string_view::npos ? next_sec : next_sec - (line_end + 1));

			// Look for separator dash: " - ", " — ", " \- "
			const std::vector<std::string_view> seps = {" - ", " — ", " \\- ", " -- "};
			for (const auto &sep : seps) {
				size_t sep_pos = name_block.find(sep);
				if (sep_pos != std::string_view::npos) {
					std::string_view desc = name_block.substr(sep_pos + sep.size());
					// Trim leading/trailing whitespace and newlines
					while (!desc.empty() && (desc.front() == ' ' || desc.front() == '\t' || desc.front() == '\n')) {
						desc.remove_prefix(1);
					}
					while (!desc.empty() && (desc.back() == ' ' || desc.back() == '\t' || desc.back() == '\n')) {
						desc.remove_suffix(1);
					}

					if (!desc.empty()) {
						std::string res(desc);
						// Replace any internal newlines with space
						std::replace(res.begin(), res.end(), '\n', ' ');
						// Capitalize first character
						res[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(res[0])));
						if (res.back() != '.') {
							res += '.';
						}
						return res;
					}
				}
			}
		}
	}

	return "";
}

cached_man_entry man_page_provider::parse_man_page(const std::string &file_path, const std::string &token)
{
	cached_man_entry entry;
	entry.exists = false;
	entry.file_path = file_path;

	std::string raw_content = read_file_or_gzip(file_path);
	if (raw_content.empty()) {
		return entry;
	}

	// Handle .so redirect directive
	if (raw_content.starts_with(".so ")) {
		std::string rel_target = raw_content.substr(4);
		while (!rel_target.empty() && (rel_target.front() == ' ' || rel_target.front() == '\t')) {
			rel_target.erase(rel_target.begin());
		}
		while (!rel_target.empty() && (rel_target.back() == ' ' || rel_target.back() == '\t' || rel_target.back() == '\n' || rel_target.back() == '\r')) {
			rel_target.pop_back();
		}

		std::filesystem::path redirect_path;
		if (rel_target.starts_with("man3/")) {
			redirect_path = std::filesystem::path(man3_dir_).parent_path() / rel_target;
		} else {
			redirect_path = std::filesystem::path(man3_dir_) / rel_target;
		}

		if (std::filesystem::exists(redirect_path)) {
			raw_content = read_file_or_gzip(redirect_path);
			entry.file_path = redirect_path.string();
		}
	}

	if (raw_content.empty()) {
		return entry;
	}

	std::string md = troff2md(raw_content);
	if (md.empty()) {
		return entry;
	}

	std::string proto = extract_synopsis_prototype(md, token);
	if (!proto.empty()) {
		entry.exists = true;
		entry.signature = std::move(proto);
		entry.summary = extract_short_description(md, token);
	}

	return entry;
}

static std::string format_man_markdown(const cached_man_entry &entry, const std::string &token)
{
	inspector::markdown_builder mb;
	mb.header("Signature:").code(entry.signature).newline();
	if (!entry.summary.empty()) {
		mb.header("Summary:").text(entry.summary).newline();
	}
	mb.header("Decl:").link(std::format("man 3 {}", token), std::format("man:{}", token));
	return mb.build();
}

inspector_result man_page_provider::query(const inspector_query &q)
{
	inspector_result res;
	res.provider_name = "man";

	if (q.token.empty()) {
		res.score = inspector_scores::INVALID;
		res.markdown = "*[No symbol under cursor]*";
		res.is_final = true;
		return res;
	}

	// 1. Check in-memory cache
	auto cached = lookup_cached(q.token);
	if (cached.has_value()) {
		if (cached->exists) {
			res.score = inspector_scores::MAN_PAGE;
			res.markdown = format_man_markdown(*cached, q.token);
			res.is_final = true;
			return res;
		}
		// Negative cache hit
		res.score = inspector_scores::INVALID;
		res.is_final = true;
		return res;
	}

	// 2. Fast filesystem probe
	std::string man_path = find_man_file(q.token);
	if (man_path.empty()) {
		// Cache negative lookup so repeated tokens fail instantly
		cached_man_entry neg;
		neg.exists = false;
		store_cached(q.token, neg);

		res.score = inspector_scores::INVALID;
		res.is_final = true;
		return res;
	}

	// File exists on disk: return provisional state and trigger query_async
	res.score = inspector_scores::RESOLVING_HINT;
	res.markdown = std::format("`{}` *[reading man page...]*", q.token);
	res.is_final = false;
	return res;
}

void man_page_provider::query_async(const inspector_query &q,
				    std::function<void(inspector_result)> callback)
{
	std::string man_path = find_man_file(q.token);
	if (man_path.empty()) {
		cached_man_entry neg;
		neg.exists = false;
		store_cached(q.token, neg);
		if (callback) {
			inspector_result res;
			res.provider_name = "man";
			res.score = inspector_scores::INVALID;
			res.is_final = true;
			callback(res);
		}
		return;
	}

	std::thread([this, q, man_path, callback]() {
		auto entry = parse_man_page(man_path, q.token);
		store_cached(q.token, entry);

		inspector_result res;
		res.provider_name = "man";
		res.is_final = true;

		if (entry.exists) {
			res.score = inspector_scores::MAN_PAGE;
			res.markdown = format_man_markdown(entry, q.token);
		} else {
			res.score = inspector_scores::INVALID;
		}

		if (callback) {
			callback(res);
		}
	}).detach();
}
