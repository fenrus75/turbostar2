#include "plugins/verilog/verilog_module_scanner.h"

#include "mapped_file.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctre.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>

namespace verilog
{

bool verilog_module_scanner::is_ignored_file(const std::filesystem::path &p)
{
	std::string fname = p.filename().string();
	if (fname.ends_with('~') || fname.ends_with(".bak") || fname.ends_with(".org") ||
	    fname.ends_with(".twoport") || fname.ends_with(".orig") || fname.starts_with(".#") ||
	    fname.starts_with('.')) {
		return true;
	}
	if (fname.starts_with("tb_") || fname.ends_with("_tb.sv") || fname.ends_with("_tb.v") ||
	    fname.find("testbench") != std::string::npos) {
		return true;
	}
	for (const auto &part : p) {
		std::string s = part.string();
		if (s == ".git" || s == ".antigravitycli" || s == ".turbostar" || s == "build" ||
		    s == "obj_dir" || s == "tb" || s == "testbench" || s == "templates") {
			return true;
		}
	}
	return false;
}

std::string verilog_module_scanner::strip_comments(std::string_view code)
{
	std::string out;
	out.reserve(code.size());
	bool in_line_comment = false;
	bool in_block_comment = false;
	bool in_string = false;

	for (size_t i = 0; i < code.size(); ++i) {
		char c = code[i];
		char next = (i + 1 < code.size()) ? code[i + 1] : '\0';

		if (in_line_comment) {
			if (c == '\n') {
				in_line_comment = false;
				out.push_back('\n');
			}
		} else if (in_block_comment) {
			if (c == '*' && next == '/') {
				in_block_comment = false;
				++i;
				out.push_back(' ');
			} else if (c == '\n') {
				out.push_back('\n');
			}
		} else if (in_string) {
			out.push_back(c);
			if (c == '\\' && next != '\0') {
				out.push_back(next);
				++i;
			} else if (c == '"') {
				in_string = false;
			}
		} else {
			if (c == '/' && next == '/') {
				in_line_comment = true;
				++i;
			} else if (c == '/' && next == '*') {
				in_block_comment = true;
				++i;
			} else if (c == '"') {
				in_string = true;
				out.push_back(c);
			} else {
				out.push_back(c);
			}
		}
	}
	return out;
}

std::vector<std::string> verilog_module_scanner::find_synthesizable_files(const std::filesystem::path &root)
{
	std::vector<std::string> sv_files;
	std::error_code ec;

	if (std::filesystem::is_regular_file(root, ec)) {
		if (!is_ignored_file(root)) {
			auto ext = root.extension().string();
			if (ext == ".sv" || ext == ".v") {
				sv_files.push_back(root.string());
			}
		}
	} else if (std::filesystem::is_directory(root, ec)) {
		for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
			if (entry.is_regular_file()) {
				if (is_ignored_file(entry.path())) {
					continue;
				}
				auto ext = entry.path().extension().string();
				if (ext == ".sv" || ext == ".v") {
					sv_files.push_back(entry.path().string());
				}
			}
		}
	}
	std::sort(sv_files.begin(), sv_files.end());
	return sv_files;
}

std::vector<discovered_module> verilog_module_scanner::discover_modules_in_files(std::span<const std::string> files)
{
	std::vector<discovered_module> results;
	for (const auto &fpath : files) {
		std::ifstream ifs(fpath);
		if (!ifs.is_open()) {
			continue;
		}

		std::string raw_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		std::string clean_content = strip_comments(raw_content);

		const char *start = clean_content.data();
		const char *end = clean_content.data() + clean_content.size();

		while (start < end) {
			auto match = ctre::search<R"(\bmodule\s+([a-zA-Z_][a-zA-Z_0-9]*)\b)">(start, end);
			if (!match) {
				break;
			}
			std::string mod_name = match.get<1>().to_string();
			if (mod_name != "endmodule" && mod_name != "begin" && mod_name != "task" && mod_name != "function") {
				std::filesystem::path src_p(fpath);
				std::filesystem::path sidecar_p = src_p.parent_path() / (mod_name + ".timing.md");
				results.push_back({std::move(mod_name), fpath, sidecar_p.string()});
			}
			start = match.data() + match.size();
		}
	}
	return results;
}

std::vector<discovered_module> verilog_module_scanner::scan(const std::filesystem::path &root, std::vector<std::string> &out_all_files)
{
	out_all_files = find_synthesizable_files(root);
	if (out_all_files.empty()) {
		return {};
	}
	return discover_modules_in_files(out_all_files);
}

bool verilog_module_scanner::is_testbench_file(const std::filesystem::path &p)
{
	std::string fname = p.filename().string();
	if (fname.ends_with('~') || fname.ends_with(".bak") || fname.ends_with(".org") ||
	    fname.ends_with(".twoport") || fname.ends_with(".orig") || fname.starts_with(".#") ||
	    fname.starts_with('.')) {
		return false;
	}
	auto ext = p.extension().string();
	if (ext != ".sv" && ext != ".v") {
		return false;
	}
	for (const auto &part : p) {
		std::string s = part.string();
		if (s == ".git" || s == ".antigravitycli" || s == ".turbostar" || s == "build" ||
		    s == "obj_dir" || s == "templates") {
			return false;
		}
	}
	if (fname.starts_with("tb_") || fname.ends_with("_tb.sv") || fname.ends_with("_tb.v") ||
	    fname.find("testbench") != std::string::npos) {
		return true;
	}
	for (const auto &part : p.parent_path()) {
		std::string s = part.string();
		if (s == "tb" || s == "testbench") {
			return true;
		}
	}
	return false;
}

std::vector<std::string> verilog_module_scanner::find_testbench_files(const std::filesystem::path &root)
{
	std::vector<std::string> tb_files;
	std::error_code ec;

	if (std::filesystem::is_regular_file(root, ec)) {
		auto ext = root.extension().string();
		if (ext == ".sv" || ext == ".v") {
			tb_files.push_back(root.string());
		}
	} else if (std::filesystem::is_directory(root, ec)) {
		for (const auto &entry : std::filesystem::recursive_directory_iterator(root, ec)) {
			if (entry.is_regular_file(ec) && is_testbench_file(entry.path())) {
				tb_files.push_back(entry.path().string());
			}
		}
	}
	std::sort(tb_files.begin(), tb_files.end());
	return tb_files;
}

std::vector<std::string> verilog_module_scanner::stage_readmem_files(std::span<const std::string> source_files, const std::filesystem::path &work_dir)
{
	std::vector<std::string> staged;
	std::error_code ec;

	for (const auto &fpath : source_files) {
		std::ifstream ifs(fpath);
		if (!ifs.is_open()) {
			continue;
		}
		std::string raw_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		std::string clean_content = strip_comments(raw_content);

		const char *start = clean_content.data();
		const char *end = clean_content.data() + clean_content.size();

		while (start < end) {
			auto match = ctre::search<R"re(\$readmem[hb]\s*\(\s*"([^"]+)")re">(start, end);
			if (!match) {
				break;
			}
			std::string ref_file = match.get<1>().to_string();
			start = match.data() + match.size();

			std::filesystem::path ref_p(ref_file);
			if (ref_p.empty() || ref_p.is_absolute() || ref_file.find("..") != std::string::npos) {
				continue;
			}

			std::filesystem::path dest_p = work_dir / ref_p;
			if (std::filesystem::exists(dest_p, ec)) {
				continue;
			}

			// Look for the referenced file next to the RTL source file first, then recursively in work_dir
			std::filesystem::path src_dir = std::filesystem::path(fpath).parent_path();
			std::filesystem::path found_p;
			if (std::filesystem::is_regular_file(src_dir / ref_p, ec)) {
				found_p = src_dir / ref_p;
			} else if (ref_p.filename() == ref_p) {
				for (const auto &entry : std::filesystem::recursive_directory_iterator(work_dir, ec)) {
					if (entry.is_regular_file(ec) && entry.path().filename() == ref_p) {
						found_p = entry.path();
						break;
					}
				}
			}

			if (!found_p.empty()) {
				if (dest_p.has_parent_path()) {
					std::filesystem::create_directories(dest_p.parent_path(), ec);
				}
				// Remove any broken symlink at dest_p before symlinking/copying
				if (std::filesystem::is_symlink(dest_p, ec)) {
					std::filesystem::remove(dest_p, ec);
				}
				std::filesystem::path rel_target = std::filesystem::relative(found_p, dest_p.parent_path(), ec);
				if (!ec && !rel_target.empty()) {
					std::filesystem::create_symlink(rel_target, dest_p, ec);
				} else {
					ec.clear();
					std::filesystem::copy_file(found_p, dest_p, std::filesystem::copy_options::overwrite_existing, ec);
				}
				if (!ec) {
					staged.push_back(ref_file);
				}
			}
		}
	}
	return staged;
}

std::string verilog_module_scanner::extract_dumpfile_path(const std::filesystem::path &testbench_file)
{
	std::ifstream ifs(testbench_file);
	if (!ifs.is_open()) {
		return "dump.vcd";
	}
	std::string raw_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	std::string clean_content = strip_comments(raw_content);

	auto match = ctre::search<R"re(\$dumpfile\s*\(\s*"([^"]+)"\s*\))re">(clean_content);
	if (match) {
		std::string p = match.get<1>().to_string();
		if (!p.empty() && p.find("..") == std::string::npos) {
			return p;
		}
	}
	return "dump.vcd";
}

std::string verilog_module_scanner::resolve_vcd_path(const std::filesystem::path &project_root, std::string_view requested_path)
{
	std::error_code ec;
	std::filesystem::path root = project_root.empty() ? std::filesystem::path(".") : project_root;

	if (!requested_path.empty()) {
		std::filesystem::path req_p(requested_path);
		if (std::filesystem::exists(req_p, ec) || std::filesystem::exists(root / req_p, ec)) {
			return std::string(requested_path);
		}
	}

	// Discover the VCD path declared by $dumpfile("...") in the project's testbench file(s)
	std::string tb_dump = "dump.vcd";
	auto tbs = find_testbench_files(root);
	for (const auto &tb : tbs) {
		std::string extracted = extract_dumpfile_path(tb);
		if (!extracted.empty()) {
			tb_dump = extracted;
			if (extracted != "dump.vcd") {
				break;
			}
		}
	}

	auto qualify_with_root = [&](const std::filesystem::path &rel_p) -> std::string {
		if (!std::filesystem::exists(rel_p, ec) && std::filesystem::exists(root / rel_p, ec)) {
			return (root / rel_p).string();
		}
		return rel_p.string();
	};

	if (requested_path.empty()) {
		return qualify_with_root(tb_dump);
	}

	// If the caller passed e.g. "sim/dump.vcd" when the testbench wrote "dump.vcd" in root, redirect to tb_dump
	std::filesystem::path req_p(requested_path);
	std::filesystem::path tb_p(tb_dump);
	if (req_p.filename() == tb_p.filename() && std::filesystem::exists(root / tb_p, ec)) {
		return qualify_with_root(tb_p);
	}
	std::filesystem::path sibling_p = tb_p.parent_path() / req_p.filename();
	if (!req_p.filename().empty() && std::filesystem::exists(root / sibling_p, ec)) {
		return qualify_with_root(sibling_p);
	}

	return std::string(requested_path);
}

std::string verilog_module_scanner::resolve_module_name(std::string_view requested_module,
                                                        std::string_view safe_file,
                                                        std::span<const std::string> all_sources)
{
	std::vector<discovered_module> file_mods;
	if (!safe_file.empty()) {
		std::string sf(safe_file);
		file_mods = discover_modules_in_files(std::span<const std::string>(&sf, 1));
		if (requested_module.empty() && !file_mods.empty()) {
			return file_mods.front().module_name;
		}
	}

	if (requested_module.empty()) {
		return "";
	}

	std::string req(requested_module);
	std::string leaf = req;
	if (req.ends_with(".sv") || req.ends_with(".v")) {
		leaf = std::filesystem::path(req).stem().string();
	} else if (auto pos = req.find_last_of("./"); pos != std::string::npos && pos + 1 < req.size()) {
		leaf = req.substr(pos + 1);
	}

	auto all_mods = discover_modules_in_files(all_sources);
	for (const auto &fm : file_mods) {
		bool exists = false;
		for (const auto &am : all_mods) {
			if (am.module_name == fm.module_name) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			all_mods.push_back(fm);
		}
	}

	// 1. Exact match on declared module name
	for (const auto &m : all_mods) {
		if (m.module_name == req || m.module_name == leaf) {
			return m.module_name;
		}
	}

	// 2. Single declared module inside an explicitly specified file
	if (file_mods.size() == 1) {
		return file_mods.front().module_name;
	}

	// 3. Match on source filename or filename stem (e.g., "alu" or "alu.sv" -> "alu_mod" in sv/alu/alu.sv)
	for (const auto &m : all_mods) {
		std::filesystem::path src_p(m.source_file);
		if (src_p.stem().string() == req || src_p.stem().string() == leaf || src_p.filename().string() == req) {
			return m.module_name;
		}
	}

	// 4. Match on _mod suffix / prefix convention
	for (const auto &m : all_mods) {
		if (m.module_name == leaf + "_mod" || leaf == m.module_name + "_mod") {
			return m.module_name;
		}
	}

	// 5. Match on RTL submodule instance name (e.g. "cpu cpu8008(" -> "cpu", "alu_mod alu(" -> "alu_mod")
	auto is_known_module = [&](std::string_view cand) -> bool {
		for (const auto &m : all_mods) {
			if (m.module_name == cand) {
				return true;
			}
		}
		return false;
	};

	for (const auto &fpath : all_sources) {
		std::ifstream ifs(fpath);
		if (!ifs.is_open()) {
			continue;
		}
		std::string raw_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		std::string clean_content = strip_comments(raw_content);

		const char *start = clean_content.data();
		const char *end = clean_content.data() + clean_content.size();
		while (start < end) {
			auto match = ctre::search<R"(\b([a-zA-Z_][a-zA-Z_0-9]*)\s+([a-zA-Z_][a-zA-Z_0-9]*)\s*\()">(start, end);
			if (!match) {
				break;
			}
			std::string mod_type = match.get<1>().to_string();
			std::string inst_name = match.get<2>().to_string();
			if (is_known_module(mod_type) && (inst_name == req || inst_name == leaf)) {
				return mod_type;
			}
			start = match.data() + match.size();
		}

		start = clean_content.data();
		while (start < end) {
			auto match = ctre::search<R"re(\b([a-zA-Z_][a-zA-Z_0-9]*)\s*#\s*\([^;]*?\)\s*([a-zA-Z_][a-zA-Z_0-9]*)\s*\()re">(start, end);
			if (!match) {
				break;
			}
			std::string mod_type = match.get<1>().to_string();
			std::string inst_name = match.get<2>().to_string();
			if (is_known_module(mod_type) && (inst_name == req || inst_name == leaf)) {
				return mod_type;
			}
			start = match.data() + match.size();
		}
	}

	return req;
}

namespace
{

constexpr bool is_sv_ident_char(char c) noexcept
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

constexpr std::string_view trim_sv(std::string_view s) noexcept
{
	size_t b = s.find_first_not_of(" \t\r\n");
	if (b == std::string_view::npos) {
		return {};
	}
	size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

bool is_valid_sv_identifier(std::string_view s) noexcept
{
	if (s.empty()) {
		return false;
	}
	char first = s.front();
	if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_')) {
		return false;
	}
	for (char c : s) {
		if (!is_sv_ident_char(c)) {
			return false;
		}
	}
	static constexpr std::string_view keywords[] = {
		"module", "endmodule", "input", "output", "inout", "logic", "wire", "reg", "bit",
		"int", "integer", "byte", "shortint", "longint", "typedef", "enum", "struct", "union",
		"begin", "end", "always", "always_ff", "always_comb", "always_latch", "assign",
		"if", "else", "case", "casex", "casez", "endcase", "default", "for", "while",
		"function", "endfunction", "task", "endtask", "package", "endpackage", "parameter",
		"localparam", "posedge", "negedge", "or", "and", "signed", "unsigned", "var", "const"
	};
	for (auto kw : keywords) {
		if (s == kw) {
			return false;
		}
	}
	return true;
}

bool parse_verilog_int_literal(std::string_view raw_rhs, uint64_t &out_val) noexcept
{
	std::string clean;
	clean.reserve(raw_rhs.size());
	for (char c : trim_sv(raw_rhs)) {
		if (c != '_' && c != ' ' && c != '\t') {
			clean.push_back(c);
		}
	}
	if (clean.empty()) {
		return false;
	}
	if (clean == "'0") {
		out_val = 0;
		return true;
	}

	int base = 10;
	std::string_view digits = clean;

	if (auto tick = clean.find('\''); tick != std::string::npos) {
		std::string_view after = std::string_view(clean).substr(tick + 1);
		if (!after.empty() && (after.front() == 's' || after.front() == 'S')) {
			after.remove_prefix(1);
		}
		if (after.empty()) {
			return false;
		}
		char bch = after.front();
		after.remove_prefix(1);
		if (bch == 'b' || bch == 'B') {
			base = 2;
		} else if (bch == 'h' || bch == 'H') {
			base = 16;
		} else if (bch == 'd' || bch == 'D') {
			base = 10;
		} else if (bch == 'o' || bch == 'O') {
			base = 8;
		} else {
			return false;
		}
		digits = after;
	} else if (clean.starts_with("0x") || clean.starts_with("0X")) {
		base = 16;
		digits = std::string_view(clean).substr(2);
	}

	if (digits.empty()) {
		return false;
	}
	for (char c : digits) {
		if (base == 2 && (c != '0' && c != '1')) return false;
		if (base == 8 && (c < '0' || c > '7')) return false;
		if (base == 10 && (c < '0' || c > '9')) return false;
		if (base == 16 && !std::isxdigit(static_cast<unsigned char>(c))) return false;
	}

	try {
		out_val = std::stoull(std::string(digits), nullptr, base);
		return true;
	} catch (...) {
		return false;
	}
}

std::map<std::string, std::string> parse_enum_body(std::string_view body)
{
	std::map<std::string, std::string> val_to_label;
	uint64_t next_val = 0;
	bool next_valid = true;

	size_t pos = 0;
	while (pos <= body.size()) {
		size_t comma = body.find(',', pos);
		std::string_view item = (comma == std::string_view::npos)
			? body.substr(pos)
			: body.substr(pos, comma - pos);
		pos = (comma == std::string_view::npos) ? body.size() + 1 : comma + 1;

		item = trim_sv(item);
		if (item.empty()) {
			continue;
		}

		auto eq = item.find('=');
		if (eq == std::string_view::npos) {
			if (is_valid_sv_identifier(item) && next_valid) {
				val_to_label[std::format("0x{:x}", next_val)] = std::string(item);
				++next_val;
			} else {
				next_valid = false;
			}
		} else {
			std::string_view name = trim_sv(item.substr(0, eq));
			std::string_view rhs = trim_sv(item.substr(eq + 1));
			uint64_t parsed = 0;
			if (is_valid_sv_identifier(name) && parse_verilog_int_literal(rhs, parsed)) {
				val_to_label[std::format("0x{:x}", parsed)] = std::string(name);
				next_val = parsed + 1;
				next_valid = true;
			} else {
				next_valid = false;
			}
		}
	}

	return val_to_label;
}

std::vector<std::string> parse_declarator_list(std::string_view decl_text, bool in_port_list)
{
	std::vector<std::string> names;
	size_t pos = 0;
	bool first_item = true;

	while (pos < decl_text.size()) {
		int paren_depth = 0;
		size_t comma = std::string_view::npos;
		for (size_t i = pos; i < decl_text.size(); ++i) {
			char c = decl_text[i];
			if (c == '(' || c == '{' || c == '[') {
				++paren_depth;
			} else if (c == ')' || c == '}' || c == ']') {
				if (paren_depth > 0) --paren_depth;
			} else if (c == ',' && paren_depth == 0) {
				comma = i;
				break;
			}
		}

		std::string_view item = (comma == std::string_view::npos)
			? decl_text.substr(pos)
			: decl_text.substr(pos, comma - pos);
		pos = (comma == std::string_view::npos) ? decl_text.size() : comma + 1;

		item = trim_sv(item);
		if (item.empty()) {
			continue;
		}

		if (auto eq = item.find('='); eq != std::string_view::npos) {
			item = trim_sv(item.substr(0, eq));
		}
		while (item.starts_with('[')) {
			auto close_br = item.find(']');
			if (close_br == std::string_view::npos) break;
			item = trim_sv(item.substr(close_br + 1));
		}
		if (auto br = item.find('['); br != std::string_view::npos) {
			item = trim_sv(item.substr(0, br));
		}

		if (in_port_list && !first_item) {
			// In ANSI port lists (e.g. "output state_t s1, input logic clk"), a comma may start a new typed port
			if (!is_valid_sv_identifier(item)) {
				break;
			}
		}
		first_item = false;

		if (is_valid_sv_identifier(item)) {
			names.emplace_back(item);
		}
	}

	return names;
}

struct module_span {
	std::string name;
	size_t start_pos{0};
	size_t header_end_pos{0};
	size_t end_pos{0};
};

std::vector<module_span> find_module_spans(std::string_view clean)
{
	std::vector<module_span> spans;
	const char *base = clean.data();
	const char *start = base;
	const char *end = base + clean.size();

	while (start < end) {
		auto match = ctre::search<R"(\bmodule\s+(?:automatic\s+|static\s+)?([a-zA-Z_][a-zA-Z_0-9]*)\b)">(start, end);
		if (!match) {
			break;
		}
		std::string mod_name = match.get<1>().to_string();
		size_t mod_start = static_cast<size_t>(match.data() - base);
		size_t after_name = static_cast<size_t>((match.data() + match.size()) - base);

		auto end_match = ctre::search<R"(\bendmodule\b)">(base + after_name, end);
		size_t mod_end = end_match ? static_cast<size_t>((end_match.data() + end_match.size()) - base) : clean.size();

		// Find header semicolon ';'
		size_t semi = clean.find(';', after_name);
		size_t header_end = (semi != std::string_view::npos && semi < mod_end) ? semi : after_name;

		spans.push_back({std::move(mod_name), mod_start, header_end, mod_end});
		start = base + mod_end;
	}
	return spans;
}

} // namespace

std::map<std::string, std::map<std::string, std::string>> verilog_module_scanner::discover_enum_signal_labels(const std::filesystem::path &root)
{
	std::vector<std::string> files = find_synthesizable_files(root);
	auto tb_files = find_testbench_files(root);
	for (auto &tb : tb_files) {
		if (std::ranges::find(files, tb) == files.end()) {
			files.push_back(std::move(tb));
		}
	}
	if (files.empty()) {
		return {};
	}

	std::map<std::string, std::string> cleaned_cache;
	auto get_clean = [&](const std::string &fpath, std::string_view raw_sv) -> std::string_view {
		auto it = cleaned_cache.find(fpath);
		if (it != cleaned_cache.end()) {
			return it->second;
		}
		auto [ins_it, _] = cleaned_cache.emplace(fpath, strip_comments(raw_sv));
		return ins_it->second;
	};

	struct enum_signal_binding {
		std::string mod_name;
		std::string sig_name;
		std::map<std::string, std::string> labels;
	};

	// type_name -> (hex_val -> label)
	std::map<std::string, std::map<std::string, std::string>> enum_types;
	std::vector<enum_signal_binding> bindings;

	// Pass 1: Use mapped_file + ::memmem("enum") fast-path to locate enum declarations
	for (const auto &fpath : files) {
		std::string err;
		auto mf = mapped_file::get_cached(fpath, err, /*populate=*/false);
		if (!mf || !mf->is_open()) {
			continue;
		}
		std::string_view raw_sv = mf->string_view();
		if (raw_sv.size() < 4 || ::memmem(raw_sv.data(), raw_sv.size(), "enum", 4) == nullptr) {
			continue;
		}

		std::string_view clean = get_clean(fpath, raw_sv);
		auto mod_spans = find_module_spans(clean);
		auto enclosing_mod = [&](size_t pos) -> std::string {
			for (const auto &ms : mod_spans) {
				if (pos >= ms.start_pos && pos < ms.end_pos) {
					return ms.name;
				}
			}
			return "";
		};

		const char *base = clean.data();
		const char *cur = base;
		const char *end = base + clean.size();

		while (cur < end) {
			auto match = ctre::search<R"(\benum\b)">(cur, end);
			if (!match) {
				break;
			}
			size_t enum_pos = static_cast<size_t>(match.data() - base);
			size_t after_enum = static_cast<size_t>((match.data() + match.size()) - base);

			// Check if preceded by 'typedef'
			bool is_typedef = false;
			size_t prev = enum_pos;
			while (prev > 0 && (clean[prev - 1] == ' ' || clean[prev - 1] == '\t' || clean[prev - 1] == '\r' || clean[prev - 1] == '\n')) {
				--prev;
			}
			if (prev >= 7 && clean.substr(prev - 7, 7) == "typedef" && (prev == 7 || !is_sv_ident_char(clean[prev - 8]))) {
				is_typedef = true;
			}

			size_t open_brace = clean.find('{', after_enum);
			size_t semi_before = clean.find(';', after_enum);
			if (open_brace == std::string_view::npos || (semi_before != std::string_view::npos && semi_before < open_brace)) {
				cur = base + after_enum;
				continue;
			}
			size_t close_brace = clean.find('}', open_brace + 1);
			if (close_brace == std::string_view::npos) {
				break;
			}
			size_t semi_after = clean.find(';', close_brace + 1);
			if (semi_after == std::string_view::npos) {
				break;
			}

			std::string_view body = clean.substr(open_brace + 1, close_brace - open_brace - 1);
			std::string_view tail = trim_sv(clean.substr(close_brace + 1, semi_after - close_brace - 1));
			auto val_map = parse_enum_body(body);

			if (!val_map.empty() && !tail.empty()) {
				if (is_typedef) {
					if (is_valid_sv_identifier(tail)) {
						enum_types[std::string(tail)] = std::move(val_map);
					}
				} else {
					std::string mod = enclosing_mod(enum_pos);
					for (auto &sig : parse_declarator_list(tail, false)) {
						bindings.push_back({mod, std::move(sig), val_map});
					}
				}
			}

			cur = base + semi_after + 1;
		}
	}

	if (enum_types.empty() && bindings.empty()) {
		return {};
	}

	// Pass 2: Find signal declarations using discovered typedef enum types (filtered via ::memmem)
	if (!enum_types.empty()) {
		for (const auto &fpath : files) {
			std::string err;
			auto mf = mapped_file::get_cached(fpath, err, /*populate=*/false);
			if (!mf || !mf->is_open()) {
				continue;
			}
			std::string_view raw_sv = mf->string_view();

			bool has_any_type = false;
			for (const auto &[tname, _] : enum_types) {
				if (raw_sv.size() >= tname.size() && ::memmem(raw_sv.data(), raw_sv.size(), tname.data(), tname.size()) != nullptr) {
					has_any_type = true;
					break;
				}
			}
			if (!has_any_type) {
				continue;
			}

			std::string_view clean = get_clean(fpath, raw_sv);
			auto mod_spans = find_module_spans(clean);

			for (const auto &[tname, val_map] : enum_types) {
				size_t search_pos = 0;
				while (search_pos < clean.size()) {
					size_t hit = clean.find(tname, search_pos);
					if (hit == std::string_view::npos) {
						break;
					}
					size_t after_hit = hit + tname.size();
					search_pos = after_hit;

					// Verify identifier word boundaries
					if ((hit > 0 && is_sv_ident_char(clean[hit - 1])) ||
					    (after_hit < clean.size() && is_sv_ident_char(clean[after_hit]))) {
						continue;
					}

					// Check preceding non-whitespace character to skip `typedef enum { ... } state_t;`
					size_t prev = hit;
					while (prev > 0 && (clean[prev - 1] == ' ' || clean[prev - 1] == '\t' || clean[prev - 1] == '\r' || clean[prev - 1] == '\n')) {
						--prev;
					}
					if (prev > 0 && (clean[prev - 1] == '}' || clean[prev - 1] == '.' || clean[prev - 1] == ':')) {
						continue;
					}

					// Skip casts like `state_t'(...)`
					size_t next = after_hit;
					while (next < clean.size() && (clean[next] == ' ' || clean[next] == '\t' || clean[next] == '\r' || clean[next] == '\n')) {
						++next;
					}
					if (next >= clean.size() || clean[next] == '\'' || clean[next] == ';' || clean[next] == '(' || clean[next] == ')') {
						continue;
					}

					std::string mod_name;
					bool in_port_list = false;
					for (const auto &ms : mod_spans) {
						if (hit >= ms.start_pos && hit < ms.end_pos) {
							mod_name = ms.name;
							in_port_list = (hit < ms.header_end_pos);
							break;
						}
					}

					size_t decl_end = clean.find(';', next);
					if (in_port_list) {
						size_t close_paren = clean.find(')', next);
						if (close_paren != std::string_view::npos && (decl_end == std::string_view::npos || close_paren < decl_end)) {
							decl_end = close_paren;
						}
					}
					if (decl_end == std::string_view::npos) {
						continue;
					}

					std::string_view decl_text = clean.substr(next, decl_end - next);
					for (auto &sig : parse_declarator_list(decl_text, in_port_list)) {
						bindings.push_back({mod_name, std::move(sig), val_map});
					}
					search_pos = decl_end + 1;
				}
			}
		}
	}

	if (bindings.empty()) {
		return {};
	}

	// Pass 3: Resolve module instance names (e.g. `sequencer_mod sequencer(` -> `sequencer`) using ::memmem
	std::set<std::string> target_mods;
	for (const auto &b : bindings) {
		if (!b.mod_name.empty()) {
			target_mods.insert(b.mod_name);
		}
	}

	std::map<std::string, std::set<std::string>> mod_instances;
	if (!target_mods.empty()) {
		for (const auto &fpath : files) {
			std::string err;
			auto mf = mapped_file::get_cached(fpath, err, /*populate=*/false);
			if (!mf || !mf->is_open()) {
				continue;
			}
			std::string_view raw_sv = mf->string_view();

			bool has_any_mod = false;
			for (const auto &mname : target_mods) {
				if (raw_sv.size() >= mname.size() && ::memmem(raw_sv.data(), raw_sv.size(), mname.data(), mname.size()) != nullptr) {
					has_any_mod = true;
					break;
				}
			}
			if (!has_any_mod) {
				continue;
			}

			std::string_view clean = get_clean(fpath, raw_sv);
			const char *start = clean.data();
			const char *end = clean.data() + clean.size();

			while (start < end) {
				auto match = ctre::search<R"(\b([a-zA-Z_][a-zA-Z_0-9]*)\s+([a-zA-Z_][a-zA-Z_0-9]*)\s*\()">(start, end);
				if (!match) {
					break;
				}
				std::string mod_type = match.get<1>().to_string();
				std::string inst_name = match.get<2>().to_string();
				if (target_mods.contains(mod_type) && is_valid_sv_identifier(inst_name)) {
					mod_instances[mod_type].insert(inst_name);
				}
				start = match.data() + match.size();
			}

			start = clean.data();
			while (start < end) {
				auto match = ctre::search<R"re(\b([a-zA-Z_][a-zA-Z_0-9]*)\s*#\s*\([^;]*?\)\s*([a-zA-Z_][a-zA-Z_0-9]*)\s*\()re">(start, end);
				if (!match) {
					break;
				}
				std::string mod_type = match.get<1>().to_string();
				std::string inst_name = match.get<2>().to_string();
				if (target_mods.contains(mod_type) && is_valid_sv_identifier(inst_name)) {
					mod_instances[mod_type].insert(inst_name);
				}
				start = match.data() + match.size();
			}
		}
	}

	// Build final signal pattern -> label map
	std::map<std::string, std::map<std::string, std::string>> result;
	std::map<std::string, std::map<std::string, std::string>> bare_sig_map;
	std::set<std::string> conflicting_bare_sigs;

	for (const auto &b : bindings) {
		if (!b.mod_name.empty()) {
			result[std::format("*{}.{}", b.mod_name, b.sig_name)] = b.labels;
			for (const auto &inst : mod_instances[b.mod_name]) {
				result[std::format("*{}.{}", inst, b.sig_name)] = b.labels;
			}
		}
		auto it = bare_sig_map.find(b.sig_name);
		if (it == bare_sig_map.end()) {
			bare_sig_map[b.sig_name] = b.labels;
		} else if (it->second != b.labels) {
			conflicting_bare_sigs.insert(b.sig_name);
		}
	}

	for (auto &[sig_name, labels] : bare_sig_map) {
		if (!conflicting_bare_sigs.contains(sig_name)) {
			result[sig_name] = std::move(labels);
		}
	}

	return result;
}

namespace
{

std::vector<std::string> extract_instantiated_mods_from_text(std::string_view text,
                                                             const std::set<std::string> &known_mods,
                                                             std::string_view self_mod)
{
	struct hit_entry {
		size_t offset;
		std::string mod_type;
	};
	std::vector<hit_entry> hits;

	const char *base = text.data();
	const char *start = base;
	const char *end = base + text.size();

	while (start < end) {
		auto match = ctre::search<R"(\b([a-zA-Z_][a-zA-Z_0-9]*)\s+([a-zA-Z_][a-zA-Z_0-9]*)\s*\()">(start, end);
		if (!match) {
			break;
		}
		std::string mod_type = match.get<1>().to_string();
		std::string inst_name = match.get<2>().to_string();
		if (mod_type != self_mod && known_mods.contains(mod_type) && is_valid_sv_identifier(inst_name)) {
			hits.push_back({static_cast<size_t>(match.data() - base), std::move(mod_type)});
		}
		start = match.data() + match.size();
	}

	start = base;
	while (start < end) {
		auto match = ctre::search<R"re(\b([a-zA-Z_][a-zA-Z_0-9]*)\s*#\s*\([^;]*?\)\s*([a-zA-Z_][a-zA-Z_0-9]*)\s*\()re">(start, end);
		if (!match) {
			break;
		}
		std::string mod_type = match.get<1>().to_string();
		std::string inst_name = match.get<2>().to_string();
		if (mod_type != self_mod && known_mods.contains(mod_type) && is_valid_sv_identifier(inst_name)) {
			hits.push_back({static_cast<size_t>(match.data() - base), std::move(mod_type)});
		}
		start = match.data() + match.size();
	}

	std::sort(hits.begin(), hits.end(), [](const hit_entry &a, const hit_entry &b) {
		return a.offset < b.offset;
	});

	std::vector<std::string> ordered;
	std::set<std::string> seen;
	for (auto &h : hits) {
		if (seen.insert(h.mod_type).second) {
			ordered.push_back(std::move(h.mod_type));
		}
	}
	return ordered;
}

} // namespace

std::vector<std::string> verilog_module_scanner::discover_instantiated_modules(const std::filesystem::path &source_file,
                                                                               std::span<const std::string> all_rtl_sources)
{
	auto all_mods = discover_modules_in_files(all_rtl_sources);
	std::set<std::string> known_mods;
	for (const auto &m : all_mods) {
		known_mods.insert(m.module_name);
	}
	if (known_mods.empty()) {
		return {};
	}

	std::string err;
	auto mf = mapped_file::get_cached(source_file.string(), err, /*populate=*/false);
	if (!mf || !mf->is_open()) {
		return {};
	}
	std::string_view raw_sv = mf->string_view();
	bool any_hit = false;
	for (const auto &mname : known_mods) {
		if (raw_sv.size() >= mname.size() && ::memmem(raw_sv.data(), raw_sv.size(), mname.data(), mname.size()) != nullptr) {
			any_hit = true;
			break;
		}
	}
	if (!any_hit) {
		return {};
	}

	std::string clean = strip_comments(raw_sv);
	auto spans = find_module_spans(clean);
	if (spans.empty()) {
		return extract_instantiated_mods_from_text(clean, known_mods, "");
	}

	std::vector<std::string> result;
	std::set<std::string> seen;
	for (const auto &ms : spans) {
		std::string_view body = std::string_view(clean).substr(ms.header_end_pos, ms.end_pos - ms.header_end_pos);
		for (auto &sub : extract_instantiated_mods_from_text(body, known_mods, ms.name)) {
			if (seen.insert(sub).second) {
				result.push_back(std::move(sub));
			}
		}
	}
	return result;
}

std::vector<std::string> verilog_module_scanner::discover_submodule_hierarchy(std::string_view root_module,
                                                                              std::span<const std::string> all_rtl_sources)
{
	if (root_module.empty()) {
		return {};
	}

	auto all_mods = discover_modules_in_files(all_rtl_sources);
	std::set<std::string> known_mods;
	for (const auto &m : all_mods) {
		known_mods.insert(m.module_name);
	}
	known_mods.insert(std::string(root_module));

	// Build adjacency map: parent_module -> ordered child_modules
	std::map<std::string, std::vector<std::string>> children_map;
	for (const auto &fpath : all_rtl_sources) {
		std::string err;
		auto mf = mapped_file::get_cached(fpath, err, /*populate=*/false);
		if (!mf || !mf->is_open()) {
			continue;
		}
		std::string_view raw_sv = mf->string_view();
		std::string clean = strip_comments(raw_sv);
		auto spans = find_module_spans(clean);
		for (const auto &ms : spans) {
			std::string_view body = std::string_view(clean).substr(ms.header_end_pos, ms.end_pos - ms.header_end_pos);
			auto subs = extract_instantiated_mods_from_text(body, known_mods, ms.name);
			auto &vec = children_map[ms.name];
			for (auto &s : subs) {
				if (std::find(vec.begin(), vec.end(), s) == vec.end()) {
					vec.push_back(std::move(s));
				}
			}
		}
	}

	std::vector<std::string> post_order;
	std::set<std::string> visited;
	auto dfs = [&](auto &self, const std::string &mod) -> void {
		if (!visited.insert(mod).second) {
			return;
		}
		auto it = children_map.find(mod);
		if (it != children_map.end()) {
			for (const auto &child : it->second) {
				self(self, child);
			}
		}
		post_order.push_back(mod);
	};

	dfs(dfs, std::string(root_module));
	return post_order;
}

} // namespace verilog
