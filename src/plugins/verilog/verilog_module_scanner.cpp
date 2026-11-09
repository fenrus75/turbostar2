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

	return std::string(requested_path);
}

} // namespace verilog
