#include "fs_utils.h"
#include "event_logger.h"
#include <fstream>
#include <sstream>
#include <lsp/json/json.h>

namespace fs_utils {
	std::filesystem::path safe_absolute(const std::filesystem::path& p) {
		if (p.empty()) {
			return p;
		}
		try {
			return std::filesystem::absolute(p).lexically_normal();
		} catch (const std::filesystem::filesystem_error& e) {
			event_logger::get_instance().log("Filesystem error resolving absolute path for '" + p.string() + "': " + e.what());
			return p.lexically_normal();
		} catch (...) {
			event_logger::get_instance().log("Unknown error resolving absolute path for '" + p.string() + "'");
			return p.lexically_normal();
		}
	}

	std::string get_compile_command_for_file(const std::string& filepath, const std::string& build_dir) {
		std::filesystem::path cc_json = std::filesystem::path(build_dir) / "compile_commands.json";
		if (!std::filesystem::exists(cc_json)) {
			cc_json = std::filesystem::path("compile_commands.json");
			if (!std::filesystem::exists(cc_json)) {
				return "";
			}
		}
		
		std::ifstream f(cc_json);
		if (!f.is_open()) return "";
		std::stringstream buffer;
		buffer << f.rdbuf();
		std::string json_str = buffer.str();
		
		try {
			lsp::json::Value val = lsp::json::parse(json_str);
			if (val.isArray()) {
				std::string target_abs = safe_absolute(filepath).lexically_normal().string();
				for (auto& entry_val : val.array()) {
					if (entry_val.isObject()) {
						auto& obj = entry_val.object();
						if (obj.contains("file") && obj.contains("command") && obj.contains("directory")) {
							std::string dir = obj.get("directory").string();
							std::string file = obj.get("file").string();
							std::string abs_path = safe_absolute(std::filesystem::path(dir) / file).lexically_normal().string();
							if (abs_path == target_abs) {
								// Found it! Run the command in the directory specified
								return "cd " + dir + " && " + obj.get("command").string();
							}
						}
					}
				}
			}
		} catch (...) {
			return "";
		}
		return "";
	}
}