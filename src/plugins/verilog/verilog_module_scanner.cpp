#include "plugins/verilog/verilog_module_scanner.h"

#include <algorithm>
#include <ctre.hpp>
#include <filesystem>
#include <fstream>

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

} // namespace verilog
