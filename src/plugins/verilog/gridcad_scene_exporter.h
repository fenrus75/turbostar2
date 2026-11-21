#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace verilog {

/**
 * @brief Exports SystemVerilog/Verilog modules via Yosys (techmap + Liberty) into gridcad scene JSON.
 */
class gridcad_scene_exporter {
    public:
	/**
	 * @brief Synthesize target_module (or top_module) with gridcad techmap/Liberty and write scene JSON.
	 */
	[[nodiscard]] static bool export_module_to_file(const std::filesystem::path &project_root,
							std::string_view target_module,
							const std::filesystem::path &out_json_path,
							std::string &out_error,
							bool bundle_submodules = true);
};

} // namespace verilog
