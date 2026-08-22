#include "run_cpp.h"
#include "command_runner.h"
#include "fs_utils.h"
#include "project_manager.h"

#include <filesystem>
#include <fstream>
#include <format>
#include <random>

namespace tools
{

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


	bool is_c_mode = (!args_.std_ver.starts_with("c++") && (args_.std_ver.starts_with("c") || args_.std_ver.starts_with("gnu")));
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

	std::string bin_path = (std::filesystem::path(tmp_dir) / std::format("probe_{}.bin", rand_id)).string();

	std::string compile_cmd = std::format("{} -std={} -O0 -g", compiler_bin, args_.std_ver);



	std::string src_dir = (std::filesystem::path(project_root) / "src").string();
	if (std::filesystem::is_directory(src_dir)) {
		compile_cmd += std::format(" -I{}", fs_utils::escape_shell_arg(src_dir));
	}

	for (const auto &inc : args_.includes) {
		std::string clean_inc = inc;
		if (clean_inc.find("://") != std::string::npos || clean_inc.starts_with("include:")) {
			auto vfs = ctx.fs_security.get_vfs();
			if (vfs && vfs->is_local_path_available(clean_inc)) {
				clean_inc = vfs->get_local_path(clean_inc);
			} else if (clean_inc.starts_with("include://")) {
				clean_inc = "/usr/include/" + clean_inc.substr(10);
			}
		}
		compile_cmd += std::format(" -I{}", fs_utils::escape_shell_arg(clean_inc));
	}

	for (const auto &def : args_.defines) {
		std::string clean_def = def;
		if (clean_def.starts_with("-D")) {
			clean_def = clean_def.substr(2);
		}
		compile_cmd += std::format(" -D{}", fs_utils::escape_shell_arg(clean_def));
	}


	compile_cmd += std::format(" {} -o {}", fs_utils::escape_shell_arg(src_path), fs_utils::escape_shell_arg(bin_path));

	for (const auto &lib : args_.libraries) {
		compile_cmd += std::format(" {}", fs_utils::escape_shell_arg(lib));
	}

	sync_command_runner compile_runner;
	compile_runner.apply_build_profile();
	compile_runner.set_project_dir(ctx.fs_security.get_working_directory().string());
	compile_runner.set_timeout(30);

	std::string compile_output = compile_runner.execute_and_get_output(compile_cmd);
	int compile_exit = compile_runner.get_exit_code();

	if (compile_exit != 0) {
		if (is_temp_src) std::filesystem::remove(src_path);
		return std::format("<cpp_execution_output>\n[Compilation: FAILED]\n{}\n</cpp_execution_output>", fs_utils::wrap_prompt_untrusted_data_tag("compiler_output", compile_output));
	}

	// Prepare execution command with LD_PRELOAD libturbocatch.so
	sync_command_runner exec_runner;
	exec_runner.apply_strict_agent_profile();
	exec_runner.set_enable_crash_catcher(true);
	exec_runner.set_project_dir(ctx.fs_security.get_working_directory().string());
	exec_runner.set_timeout(args_.timeout);

	std::string exec_output = exec_runner.execute_and_get_output(fs_utils::escape_shell_arg(bin_path));
	int exec_exit = exec_runner.get_exit_code();

	// Clean up temp files
	if (is_temp_src) std::filesystem::remove(src_path);
	std::filesystem::remove(bin_path);

	std::string status_str = (exec_exit == 0) ? "SUCCESS" : std::format("FAILED (Exit Code: {})", exec_exit);
	return std::format("<cpp_execution_output>\n[Execution: {}]\n{}\n</cpp_execution_output>", status_str, fs_utils::wrap_prompt_untrusted_data_tag("stdout", exec_output));
}

} // namespace tools
