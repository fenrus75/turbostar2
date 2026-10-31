#include "run_cpp.h"
#include "command_runner.h"
#include "config_manager.h"
#include "crashdump_manager.h"
#include "fs_utils.h"
#include "project_manager.h"


#include <filesystem>
#include <fstream>
#include <format>
#include <random>
#include <set>
#include <sstream>
#include <string_view>
#include <ctre.hpp>

namespace tools
{

namespace
{

struct lib_hint {
	std::vector<std::string_view> prefixes;
	std::vector<std::string_view> exact_symbols;
	std::string_view package;
	std::string_view ldflag;
};

static const std::vector<lib_hint> KNOWN_HINTS = {
	{{"sqlite3_"}, {}, "sqlite3", "-lsqlite3"},
	{{"deflate", "inflate", "zlib_"}, {"crc32", "adler32", "zlibVersion"}, "zlib", "-lz"},
	{{"png_"}, {}, "libpng", "-lpng"},
	{{"curl_"}, {}, "libcurl", "-lcurl"},
	{{"SSL_", "TLS_", "EVP_", "SHA256_", "SHA512_", "MD5_", "BIO_", "CRYPTO_"}, {"OpenSSL_version"}, "openssl", "-lssl -lcrypto"},
	{{}, {"initscr", "endwin", "wrefresh", "mvwprintw", "waddch", "nodelay", "keypad", "cbreak", "raw", "noraw", "echo", "noecho"}, "ncursesw", "-lncursesw"},
	{{"pthread_"}, {"sem_init", "sem_wait", "sem_post", "sem_destroy"}, "", "-pthread"},
	{{}, {"cos", "sin", "tan", "acos", "asin", "atan", "atan2", "cosh", "sinh", "tanh", "exp", "log", "log10", "pow", "sqrt", "ceil", "floor", "fmod", "hypot", "round", "fabs"}, "", "-lm"},
	{{}, {"dlopen", "dlclose", "dlsym", "dlerror"}, "", "-ldl"},
	{{"pcre2_"}, {}, "libpcre2-8", "-lpcre2-8"},
	{{"git_"}, {}, "libgit2", "-lgit2"},
};

std::string generate_linker_diagnostic_tip(/* untrusted */ std::string_view untrusted_output)
{
	bool is_link_failure = (untrusted_output.find("collect2: error: ld returned") != std::string_view::npos ||
				untrusted_output.find("linker command failed") != std::string_view::npos ||
				untrusted_output.find("/bin/ld:") != std::string_view::npos ||
				untrusted_output.find("undefined reference to") != std::string_view::npos ||
				untrusted_output.find("undefined symbol:") != std::string_view::npos);
	if (!is_link_failure) {
		return "";
	}

	std::vector<std::string> cannot_find_libs;
	for (auto m : ctre::search_all<R"(cannot find -l([^:\s]+))">(untrusted_output)) {
		std::string untrusted_lib(m.get<1>().to_view());
		if (fs_utils::is_safe_for_ui(untrusted_lib)) {
			cannot_find_libs.push_back(untrusted_lib);
		}
	}
	if (!cannot_find_libs.empty()) {
		std::string libs_str;
		for (size_t i = 0; i < cannot_find_libs.size(); ++i) {
			if (i > 0) libs_str += ", ";
			libs_str += std::format("'-l{}'", cannot_find_libs[i]);
		}
		return std::format("\n[Tip: Linker could not find {}. Verify that the library is installed, check library search paths via 'ldflags: [\"-L...\"]', or resolve via 'packages: [\"...\"]'.]", libs_str);
	}

	std::vector<std::string> undef_symbols;
	std::set<std::string> seen_symbols;
	for (auto m : ctre::search_all<R"((?:undefined reference to [`'\"‘]([^`'\"’\n]+)[`'\"’]|undefined symbol: ([a-zA-Z0-9_:]+)))">(untrusted_output)) {
		std::string_view s1 = m.get<1>().to_view();
		std::string_view s2 = m.get<2>().to_view();
		std::string untrusted_sym(!s1.empty() ? s1 : s2);
		if (fs_utils::is_safe_for_ui(untrusted_sym) && !seen_symbols.contains(untrusted_sym)) {
			seen_symbols.insert(untrusted_sym);
			undef_symbols.push_back(untrusted_sym);
			if (undef_symbols.size() >= 5) break;
		}
	}

	if (undef_symbols.empty()) {
		return "\n[Tip: Link failure detected. You can link dependencies via 'packages: [\"...\"]' (e.g. packages: [\"sqlite3\"]) or 'ldflags: [\"-l...\"]', or supply additional translation units via 'sources: [\"path/to/file.cpp\"]'.]";
	}

	std::vector<std::string> matched_pkgs;
	std::vector<std::string> matched_ldflags;
	std::set<std::string> seen_pkgs;
	std::set<std::string> seen_ldflags;
	std::vector<std::string> matched_syms;

	for (const auto &sym : undef_symbols) {
		for (const auto &hint : KNOWN_HINTS) {
			bool matched = false;
			for (const auto &p : hint.prefixes) {
				if (sym.starts_with(p)) { matched = true; break; }
			}
			if (!matched) {
				for (const auto &e : hint.exact_symbols) {
					if (sym == e) { matched = true; break; }
				}
			}
			if (matched) {
				matched_syms.push_back(sym);
				if (!hint.package.empty() && !seen_pkgs.contains(std::string(hint.package))) {
					seen_pkgs.insert(std::string(hint.package));
					matched_pkgs.push_back(std::string(hint.package));
				}
				if (!hint.ldflag.empty() && !seen_ldflags.contains(std::string(hint.ldflag))) {
					seen_ldflags.insert(std::string(hint.ldflag));
					matched_ldflags.push_back(std::string(hint.ldflag));
				}
				break;
			}
		}
	}

	if (!matched_pkgs.empty() || !matched_ldflags.empty()) {
		std::string syms_str;
		for (size_t i = 0; i < matched_syms.size(); ++i) {
			if (i > 0) syms_str += ", ";
			syms_str += std::format("'{}'", matched_syms[i]);
		}
		std::string rec;
		if (!matched_pkgs.empty()) {
			std::string pkgs_json = "[";
			for (size_t i = 0; i < matched_pkgs.size(); ++i) {
				if (i > 0) pkgs_json += ", ";
				pkgs_json += std::format("\"{}\"", matched_pkgs[i]);
			}
			pkgs_json += "]";
			rec += std::format("packages: {}", pkgs_json);
		}
		if (!matched_ldflags.empty()) {
			if (!rec.empty()) rec += " or ";
			std::string ld_json = "[";
			for (size_t i = 0; i < matched_ldflags.size(); ++i) {
				if (i > 0) ld_json += ", ";
				ld_json += std::format("\"{}\"", matched_ldflags[i]);
			}
			ld_json += "]";
			rec += std::format("ldflags: {}", ld_json);
		}
		return std::format("\n[Tip: Link failure for symbol(s) {}. Recommended fix: add {}.]", syms_str, rec);
	}

	return std::format("\n[Tip: Link failure for symbol '{}' (undefined reference). You can link dependencies via 'packages: [\"...\"]' (e.g. packages: [\"sqlite3\"]) or 'ldflags: [\"-l...\"]', or supply additional translation units via 'sources: [\"path/to/file.cpp\"]'.]", undef_symbols.front());
}

} // namespace

run_cpp_tool::run_cpp_tool(run_cpp_args args)
	: args_(std::move(args))
{
}

bool run_cpp_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string run_cpp_tool::execute(agentlib::tool_context &ctx)
{
	std::string src_path = args_.safe_path;
	bool is_temp_src = false;

	std::string rand_id;
	{
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<uint64_t> dist;
		rand_id = std::format("{:x}", dist(gen));
	}

	std::string project_root = project_manager::get_instance().get_project_root();
	if (project_root.empty()) {
		project_root = ctx.fs_security.get_working_directory().string();
	}
	std::string tmp_dir = (std::filesystem::path(project_root) / "build" / "tmp_cpp").string();
	if (!std::filesystem::exists(std::filesystem::path(project_root) / "build")) {
		tmp_dir = (std::filesystem::path(project_root) / ".tmp_cpp").string();
	}
	std::filesystem::create_directories(tmp_dir);


	bool is_c_mode = false;
	if (!args_.path.empty() && (args_.path.ends_with(".c") || args_.path.ends_with(".C"))) {
		is_c_mode = true;
	} else {
		for (const auto &flag : args_.cflags) {
			if (flag.starts_with("-std=")) {
				std::string ver = flag.substr(5);
				if (!ver.starts_with("c++") && (ver.starts_with("c") || ver.starts_with("gnu"))) {
					is_c_mode = true;
				}
				break;
			}
		}
	}
	std::string compiler_bin = is_c_mode ? "gcc" : "g++";
	std::string src_ext = is_c_mode ? ".c" : ".cpp";

	if (!args_.code.empty()) {
		src_path = (std::filesystem::path(tmp_dir) / std::format("probe_{}{}", rand_id, src_ext)).string();
		is_temp_src = true;

		std::string code_to_write = args_.code;
		if (code_to_write.find("main(") == std::string::npos && code_to_write.find("main (") == std::string::npos) {
			if (is_c_mode) {
				code_to_write = std::format(
					"#include <stdio.h>\n"
					"#include <stdlib.h>\n"
					"#include <stdbool.h>\n"
					"#include <string.h>\n"
					"#include <stdint.h>\n"
					"int main(void) {{\n"
					"{}\n"
					"\treturn 0;\n"
					"}}\n",
					code_to_write
				);
			} else {
				code_to_write = std::format(
					"#include <iostream>\n"
					"#include <vector>\n"
					"#include <string>\n"
					"#include <string_view>\n"
					"#include <optional>\n"
					"#include <variant>\n"
					"#include <memory>\n"
					"int main() {{\n"
					"{}\n"
					"\treturn 0;\n"
					"}}\n",
					code_to_write
				);
			}
		}

		std::ofstream ofs(src_path, std::ios::binary);
		if (!ofs) {
			return "<cpp_execution_output>\n[Error: Failed to create temporary source file]\n</cpp_execution_output>";
		}
		ofs.write(code_to_write.data(), code_to_write.size());
		ofs.close();
	} else if (!args_.path.empty()) {
		std::string raw_path = args_.safe_path.empty() ? args_.path : args_.safe_path;
		if (raw_path.find("://") != std::string::npos || raw_path.starts_with("include:")) {
			auto vfs = ctx.fs_security.get_vfs();
			if (vfs) {
				if (vfs->is_local_path_available(raw_path)) {
					std::string local_p = vfs->get_local_path(raw_path);
					if (!local_p.empty() && std::filesystem::exists(local_p)) {
						src_path = local_p;
					}
				}
				if (src_path == args_.safe_path || src_path.find("://") != std::string::npos) {
					auto handle = vfs->read_file(raw_path);
					if (handle.has_value()) {
						std::string_view vfs_code = (*handle)->view();
						src_path = (std::filesystem::path(tmp_dir) / std::format("vfs_src_{}{}", rand_id, src_ext)).string();
						is_temp_src = true;

						std::ofstream ofs(src_path, std::ios::binary);
						if (!ofs) {
							return "<cpp_execution_output>\n[Error: Failed to create temporary source file from VFS]\n</cpp_execution_output>";
						}
						ofs.write(vfs_code.data(), vfs_code.size());
						ofs.close();
					} else {
						return std::format("<cpp_execution_output>\n[Error: Failed to read source file from VFS: {}]\n</cpp_execution_output>", raw_path);
					}
				}
			}
		}
	}

	// Resolve any pkg-config packages requested
	if (!args_.packages.empty()) {
		std::string pkg_cmd = "pkg-config --cflags --libs";
		for (const auto &pkg : args_.packages) {
			if (!fs_utils::is_shell_safe(pkg)) {
				if (is_temp_src) std::filesystem::remove(src_path);
				return std::format("<cpp_execution_output>\n[pkg-config: Error: Unsafe package name '{}']\n</cpp_execution_output>",
					fs_utils::wrap_prompt_untrusted_data_tag("package", pkg));
			}
			pkg_cmd += std::format(" {}", fs_utils::escape_shell_arg(pkg));
		}

		sync_command_runner pkg_runner;
		pkg_runner.apply_build_profile();
		pkg_runner.set_project_dir(ctx.fs_security.get_working_directory().string());
		pkg_runner.set_timeout(10);

		std::string pkg_output = pkg_runner.execute_and_get_output(pkg_cmd);
		int pkg_exit = pkg_runner.get_exit_code();
		if (pkg_exit != 0) {
			if (is_temp_src) std::filesystem::remove(src_path);
			return std::format("<cpp_execution_output>\n[pkg-config: FAILED]\n{}\n</cpp_execution_output>",
				fs_utils::wrap_prompt_untrusted_data_tag("pkg_config_output", pkg_output));
		}

		std::istringstream iss(pkg_output);
		std::string token;
		while (iss >> token) {
			if (token.starts_with("-l") || token.starts_with("-L") || token.starts_with("-Wl,")) {
				args_.ldflags.push_back(token);
			} else {
				args_.cflags.push_back(token);
			}
		}
	}

	std::string bin_path = (std::filesystem::path(tmp_dir) / std::format("probe_{}.bin", rand_id)).string();

	// Check if an optimization flag is already provided in cflags
	bool has_opt = false;
	for (const auto &flag : args_.cflags) {
		if (flag.starts_with("-O")) {
			has_opt = true;
			break;
		}
	}

	std::string compile_cmd = std::format("{} -g -rdynamic", compiler_bin);
	if (!has_opt) {
		compile_cmd += " -O0";
	}

	std::string src_dir = (std::filesystem::path(project_root) / "src").string();
	if (std::filesystem::is_directory(src_dir)) {
		compile_cmd += std::format(" -I{}", fs_utils::escape_shell_arg(src_dir));
	}

	std::vector<std::string> temp_staged_files;

	// Process cflags (with transparent VFS resolution for -I paths)
	for (const auto &flag : args_.cflags) {
		std::string clean_flag = flag;
		if (clean_flag.starts_with("-I")) {
			std::string inc_path = clean_flag.substr(2);
			while (!inc_path.empty() && std::isspace(static_cast<unsigned char>(inc_path.front()))) {
				inc_path.erase(0, 1);
			}

			if (inc_path.starts_with("include://") || inc_path.starts_with("include:/") || inc_path.starts_with("include:")) {
				auto vfs = ctx.fs_security.get_vfs();
				std::string local_path;
				if (vfs && vfs->is_local_path_available(inc_path)) {
					local_path = vfs->get_local_path(inc_path);
				}
				if (local_path.empty()) {
					// Direct fallback to /usr/include/...
					std::string sub = inc_path;
					if (sub.starts_with("include://")) sub = sub.substr(10);
					else if (sub.starts_with("include:/")) sub = sub.substr(9);
					else if (sub.starts_with("include:")) sub = sub.substr(8);
					while (!sub.empty() && sub.front() == '/') sub.erase(0, 1);
					local_path = sub.empty() ? "/usr/include" : ("/usr/include/" + sub);
				}
				clean_flag = std::format("-I{}", local_path);
			} else if (inc_path.starts_with("tmp://") || inc_path.starts_with("tmp:/") || inc_path.starts_with("tmp:")) {
				auto vfs = ctx.fs_security.get_vfs();
				std::string local_path;
				if (vfs && vfs->is_local_path_available(inc_path)) {
					local_path = vfs->get_local_path(inc_path);
				}
				if (!local_path.empty() && std::filesystem::is_directory(local_path)) {
					clean_flag = std::format("-I{}", local_path);
				}
				if (vfs) {
					// Virtual in-memory files under tmp:// - stage files to tmp_dir
					auto file_list = vfs->list_directory(inc_path);
					for (const auto &vf : file_list) {
						if (vf.type == 'F' || vf.type == 'f') {
							auto handle = vfs->read_file(vf.uri);
							if (handle.has_value()) {
								std::string fname = vf.uri;
								size_t slash = fname.rfind('/');
								if (slash != std::string::npos) {
									fname = fname.substr(slash + 1);
								}
								std::filesystem::path target_header = std::filesystem::path(tmp_dir) / fname;
								std::ofstream h_ofs(target_header, std::ios::binary);
								if (h_ofs) {
									std::string_view h_code = (*handle)->view();
									h_ofs.write(h_code.data(), h_code.size());
									h_ofs.close();
									temp_staged_files.push_back(target_header.string());
								}
							}
						}
					}
					if (clean_flag == flag) {
						clean_flag = std::format("-I{}", tmp_dir);
					}
				}
			}
		}
		compile_cmd += std::format(" {}", fs_utils::escape_shell_arg(clean_flag));
	}
	compile_cmd += std::format(" -I{}", fs_utils::escape_shell_arg(tmp_dir));

	// Primary source file
	compile_cmd += std::format(" {}", fs_utils::escape_shell_arg(src_path));

	// Extra translation units (sources)
	for (const auto &src : args_.sources) {
		std::string clean_src = src;
		if (clean_src.find("://") != std::string::npos || clean_src.starts_with("tmp:")) {
			auto vfs = ctx.fs_security.get_vfs();
			if (vfs) {
				if (vfs->is_local_path_available(clean_src)) {
					std::string local_p = vfs->get_local_path(clean_src);
					if (!local_p.empty() && std::filesystem::exists(local_p)) {
						clean_src = local_p;
					}
				}
				if (clean_src == src || clean_src.find("://") != std::string::npos) {
					auto handle = vfs->read_file(src);
					if (handle.has_value()) {
						std::string_view src_code = (*handle)->view();
						std::filesystem::path filename = std::filesystem::path(src).filename();
						std::string src_temp_path = (std::filesystem::path(tmp_dir) / std::format("vfs_src_{}_{}", rand_id, filename.string())).string();

						std::ofstream s_ofs(src_temp_path, std::ios::binary);
						if (s_ofs) {
							s_ofs.write(src_code.data(), src_code.size());
							s_ofs.close();
							clean_src = src_temp_path;
							temp_staged_files.push_back(src_temp_path);
						}
					}
				}
			}
		}
		compile_cmd += std::format(" {}", fs_utils::escape_shell_arg(clean_src));
	}

	// Output binary
	compile_cmd += std::format(" -o {}", fs_utils::escape_shell_arg(bin_path));

	// Linker flags (placed after sources)
	for (const auto &ld : args_.ldflags) {
		compile_cmd += std::format(" {}", fs_utils::escape_shell_arg(ld));
	}

	sync_command_runner compile_runner;
	compile_runner.apply_build_profile();
	compile_runner.set_project_dir(ctx.fs_security.get_working_directory().string());
	compile_runner.set_timeout(30);

	std::string untrusted_compile_output = compile_runner.execute_and_get_output(compile_cmd);
	int compile_exit = compile_runner.get_exit_code();

	if (compile_exit != 0) {
		if (is_temp_src) std::filesystem::remove(src_path);
		for (const auto &tf : temp_staged_files) std::filesystem::remove(tf);
		std::string linker_tip = generate_linker_diagnostic_tip(untrusted_compile_output);
		return std::format("<cpp_execution_output>\n[Compilation: FAILED]\n{}{}\n</cpp_execution_output>",
			fs_utils::wrap_prompt_untrusted_data_tag("compiler_output", untrusted_compile_output),
			linker_tip);
	}

	// Prepare execution command with LD_PRELOAD libturbocatch.so
	sync_command_runner exec_runner;
	exec_runner.apply_strict_agent_profile();
	if (config_manager::get_instance().is_allow_code_execution_network()) {
		exec_runner.set_network_access(true);
	}
	exec_runner.set_enable_crash_catcher(true);
	exec_runner.set_project_dir(ctx.fs_security.get_working_directory().string());
	exec_runner.set_timeout(args_.timeout);

	std::string exec_output = exec_runner.execute_and_get_output(fs_utils::escape_shell_arg(bin_path));
	int exec_exit = exec_runner.get_exit_code();
	std::string crash_dumps = exec_runner.get_new_crashdumps();

	// Clean up temp files
	if (crash_dumps.empty()) {
		if (is_temp_src) std::filesystem::remove(src_path);
		for (const auto &tf : temp_staged_files) std::filesystem::remove(tf);
		if (exec_exit == 0) {
			std::filesystem::remove(bin_path);
		}
	}



	std::string status_str;
	if (exec_exit == 0) {
		status_str = "SUCCESS";
	} else {
		std::string sig_name;
		if (exec_exit == 139 || crash_dumps.find("| 11 |") != std::string::npos || crash_dumps.find("Signal | 11") != std::string::npos) {
			sig_name = "SIGSEGV (Segmentation Fault)";
		} else if (exec_exit == 134 || crash_dumps.find("| 6 |") != std::string::npos || crash_dumps.find("Signal | 6") != std::string::npos) {
			sig_name = "SIGABRT (Aborted)";
		} else if (exec_exit == 136 || crash_dumps.find("| 8 |") != std::string::npos || crash_dumps.find("Signal | 8") != std::string::npos) {
			sig_name = "SIGFPE (Floating Point Exception)";
		} else if (exec_exit == 135 || crash_dumps.find("| 7 |") != std::string::npos || crash_dumps.find("Signal | 7") != std::string::npos) {
			sig_name = "SIGBUS (Bus Error)";
		} else if (exec_exit > 128 && exec_exit < 255) {
			sig_name = std::format("Signal {}", exec_exit - 128);
		}

		if (!sig_name.empty()) {
			status_str = std::format("CRASHED ({})", sig_name);
		} else if (!crash_dumps.empty()) {
			status_str = "CRASHED";
		} else {
			status_str = std::format("FAILED (Exit Code: {})", exec_exit);
		}
	}

	std::string body = std::format("[Execution: {}]\n{}", status_str, fs_utils::wrap_prompt_untrusted_data_tag("stdout", exec_output));
	if (!crash_dumps.empty()) {
		body += std::format("\n{}", fs_utils::wrap_prompt_untrusted_data_tag("crash_report", crash_dumps));
	}

	return std::format("<cpp_execution_output>\n{}\n</cpp_execution_output>", body);
}



} // namespace tools
