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
	for (size_t i = 0; i < combined.size(); ++i) {
		char c = combined[i];
		if (c == ' ' || c == '\t' || c == '\n') {
			if (!in_space && !collapsed.empty()) {
				collapsed += ' ';
				in_space = true;
			}
		} else {
			if (in_space && (c == ',' || c == ';' || c == ')' || c == ']')) {
				if (!collapsed.empty() && collapsed.back() == ' ') {
					collapsed.pop_back();
				}
			} else if (in_space && c == '(') {
				// If followed by '*', keep space (e.g. "int (*compar)")
				bool is_fn_ptr = false;
				size_t next = i + 1;
				while (next < combined.size() && (combined[next] == ' ' || combined[next] == '\t')) {
					next++;
				}
				if (next < combined.size() && combined[next] == '*') {
					is_fn_ptr = true;
				}
				if (!is_fn_ptr && !collapsed.empty() && collapsed.back() == ' ') {
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

	// Find the end of SYNOPSIS (next section header '\n# ')
	size_t next_sec = syn_block.find("\n# ");
	if (next_sec != std::string_view::npos) {
		syn_block = syn_block.substr(0, next_sec);
	}

	// 2. Search for token followed by '(' with word boundary before token
	size_t t_pos = 0;
	size_t match_pos = std::string_view::npos;
	while ((t_pos = syn_block.find(token, t_pos)) != std::string_view::npos) {
		bool boundary_before = (t_pos == 0 ||
		                        (!std::isalnum(static_cast<unsigned char>(syn_block[t_pos - 1])) &&
		                         syn_block[t_pos - 1] != '_'));
		size_t after = t_pos + token.size();
		while (after < syn_block.size() && (syn_block[after] == ' ' || syn_block[after] == '*' || syn_block[after] == '`')) {
			after++;
		}
		bool is_call = (after < syn_block.size() && syn_block[after] == '(');

		if (boundary_before && is_call) {
			match_pos = t_pos;
			break;
		}
		t_pos += token.size();
	}

	if (match_pos == std::string_view::npos) {
		return "";
	}

	// 3. Scan backwards from match_pos to find the start of the declaration statement
	size_t start_pos = match_pos;
	while (start_pos > 0) {
		char c = syn_block[start_pos - 1];
		if (c == ';') {
			// Found previous statement end
			break;
		}
		if (c == '`') {
			// If we hit backtick code fence "```"
			if (start_pos >= 3 && syn_block.substr(start_pos - 3, 3) == "```") {
				break;
			}
		}
		start_pos--;
	}

	// If start_pos contains code fence language or #include, advance past it
	std::string_view candidate = syn_block.substr(start_pos);
	size_t inc_pos = candidate.find("#include");
	if (inc_pos != std::string_view::npos && inc_pos < (match_pos - start_pos)) {
		size_t inc_end = candidate.find('\n', inc_pos);
		if (inc_end != std::string_view::npos && inc_end < (match_pos - start_pos)) {
			start_pos += (inc_end + 1);
		}
	}

	// Skip any leading whitespace or newlines or code fence backticks
	while (start_pos < match_pos) {
		char c = syn_block[start_pos];
		if (c == ' ' || c == '\t' || c == '\n' || c == '`' || c == '\r') {
			start_pos++;
		} else if (start_pos + 1 < match_pos && syn_block.substr(start_pos, 2) == "c\n") {
			start_pos += 2;
		} else {
			break;
		}
	}

	// 4. Scan forwards from match_pos to find the terminating ';' when paren depth is 0
	int paren_depth = 0;
	bool has_opened_paren = false;
	size_t semi_pos = std::string_view::npos;
	size_t k = match_pos;
	bool in_block_comment = false;

	while (k < syn_block.size()) {
		// Handle block comments /* ... */
		if (!in_block_comment && k + 1 < syn_block.size() && syn_block[k] == '/' && syn_block[k + 1] == '*') {
			in_block_comment = true;
			k += 2;
			continue;
		}
		if (in_block_comment) {
			if (k + 1 < syn_block.size() && syn_block[k] == '*' && syn_block[k + 1] == '/') {
				in_block_comment = false;
				k += 2;
				continue;
			}
			k++;
			continue;
		}

		char c = syn_block[k];
		if (c == '(') {
			paren_depth++;
			has_opened_paren = true;
		} else if (c == ')') {
			if (paren_depth > 0) {
				paren_depth--;
			}
		} else if (c == ';') {
			// Semicolon is only terminating if all parentheses are balanced (depth == 0)
			if (has_opened_paren && paren_depth == 0) {
				semi_pos = k + 1; // include ';'
				break;
			}
		} else if (c == '\n' && k + 2 < syn_block.size() && syn_block[k + 1] == '#' && syn_block[k + 2] == ' ') {
			// Hit next section heading
			break;
		}

		k++;
	}

	// Fallback if no ';' was found after balanced parens
	if (semi_pos == std::string_view::npos && has_opened_paren && paren_depth == 0) {
		size_t last_paren = syn_block.rfind(')', k);
		if (last_paren != std::string_view::npos && last_paren >= match_pos) {
			semi_pos = last_paren + 1;
		}
	}

	if (semi_pos != std::string_view::npos && semi_pos > start_pos) {
		std::string_view raw = syn_block.substr(start_pos, semi_pos - start_pos);
		return collapse_prototype(raw);
	}

	return "";
}

std::vector<std::string> man_page_provider::extract_synopsis_headers(std::string_view full_md, std::string_view token)
{
	std::vector<std::string> headers;
	if (full_md.empty()) {
		return headers;
	}

	// 1. Locate SYNOPSIS section
	size_t syn_pos = full_md.find("SYNOPSIS");
	if (syn_pos == std::string_view::npos) {
		syn_pos = full_md.find("Synopsis");
		if (syn_pos == std::string_view::npos) {
			syn_pos = full_md.find("synopsis");
		}
	}
	if (syn_pos == std::string_view::npos) {
		return headers;
	}

	size_t line_end = full_md.find('\n', syn_pos);
	if (line_end == std::string_view::npos) {
		return headers;
	}
	std::string_view syn_block = full_md.substr(line_end + 1);
	size_t next_sec = syn_block.find("\n# ");
	if (next_sec != std::string_view::npos) {
		syn_block = syn_block.substr(0, next_sec);
	}

	// 2. If token is specified, narrow scope to before the token if #include appears before it
	std::string_view scope = syn_block;
	if (!token.empty()) {
		size_t t_pos = 0;
		while ((t_pos = syn_block.find(token, t_pos)) != std::string_view::npos) {
			bool boundary_before = (t_pos == 0 ||
			                        (!std::isalnum(static_cast<unsigned char>(syn_block[t_pos - 1])) &&
			                         syn_block[t_pos - 1] != '_'));
			size_t after = t_pos + token.size();
			while (after < syn_block.size() && (syn_block[after] == ' ' || syn_block[after] == '*' || syn_block[after] == '`')) {
				after++;
			}
			bool is_call = (after < syn_block.size() && syn_block[after] == '(');
			if (boundary_before && is_call) {
				std::string_view before = syn_block.substr(0, t_pos);
				if (before.find("#include") != std::string_view::npos) {
					scope = before;
				}
				break;
			}
			t_pos += token.size();
		}
	}

	// 3. Search for #include and extract <header> or "header"
	size_t inc_pos = 0;
	while ((inc_pos = scope.find("#include", inc_pos)) != std::string_view::npos) {
		size_t line_limit = scope.find('\n', inc_pos);
		if (line_limit == std::string_view::npos) {
			line_limit = scope.size();
		}
		std::string_view line = scope.substr(inc_pos, line_limit - inc_pos);

		// Look for '<' ... '>' or '"' ... '"'
		size_t angle_open = line.find('<');
		size_t quote_open = line.find('"');

		std::string extracted;
		if (angle_open != std::string_view::npos) {
			size_t angle_close = line.find('>', angle_open + 1);
			if (angle_close != std::string_view::npos) {
				extracted = std::string(line.substr(angle_open, angle_close - angle_open + 1));
			}
		} else if (quote_open != std::string_view::npos) {
			size_t quote_close = line.find('"', quote_open + 1);
			if (quote_close != std::string_view::npos) {
				extracted = std::string(line.substr(quote_open, quote_close - quote_open + 1));
			}
		}

		if (!extracted.empty()) {
			// Strip any inner backticks or asterisks
			std::string cleaned;
			for (char c : extracted) {
				if (c != '`' && c != '*') {
					cleaned += c;
				}
			}
			if (cleaned.size() >= 3 &&
			    std::find(headers.begin(), headers.end(), cleaned) == headers.end()) {
				headers.push_back(std::move(cleaned));
			}
		}

		inc_pos = line_limit;
	}

	return headers;
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
		entry.headers = extract_synopsis_headers(md, token);
		entry.summary = extract_short_description(md, token);
	}

	return entry;
}

static std::string format_man_markdown(const cached_man_entry &entry, const std::string &token)
{
	inspector::markdown_builder mb;
	mb.header("Signature:").code(entry.signature).newline();
	if (!entry.headers.empty()) {
		mb.header(entry.headers.size() > 1 ? "Headers:" : "Header:");
		for (size_t i = 0; i < entry.headers.size() && i < 3; ++i) {
			if (i > 0) {
				mb.text(" ");
			}
			mb.code(entry.headers[i]);
		}
		if (entry.headers.size() > 3) {
			mb.text(" ...");
		}
		mb.text("  ");
	}
	mb.header("Decl:").link(std::format("man 3 {}", token), std::format("man:{}", token)).newline();
	if (!entry.summary.empty()) {
		mb.header("Summary:").text(entry.summary).newline();
	}
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
