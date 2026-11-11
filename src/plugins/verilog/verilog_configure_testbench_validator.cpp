#include "plugins/verilog/verilog_configure_testbench_validator.h"

#include "agentlib/tool_registry.h"
#include "fs_utils.h"
#include "plugins/verilog/fpga_project_settings.h"

namespace tools
{

bool verilog_configure_testbench_validator::is_pure(const nlohmann::json &untrusted_args) const
{
	if (!untrusted_args.is_object()) {
		return true;
	}
	bool has_tb = untrusted_args.contains("testbench") && untrusted_args["testbench"].is_string() &&
	              !untrusted_args["testbench"].get<std::string>().empty();
	bool has_remove = untrusted_args.contains("remove") && untrusted_args["remove"].is_boolean() &&
	                  untrusted_args["remove"].get<bool>();
	bool has_modules = untrusted_args.contains("modules") && !untrusted_args["modules"].empty();
	bool has_level = untrusted_args.contains("level") && untrusted_args["level"].is_string() &&
	                 !untrusted_args["level"].get<std::string>().empty();
	bool has_hier = untrusted_args.contains("hierarchical") && untrusted_args["hierarchical"].is_boolean();
	return !has_tb && !has_remove && !has_modules && !has_level && !has_hier;
}

std::string verilog_configure_testbench_validator::get_description() const
{
	return "Configures, lists, or removes associations between SystemVerilog/Verilog testbenches (e.g. 'tb/tb_alu.sv') "
	       "and the RTL modules they verify (e.g. 'alu_mod' or 'sv/alu/alu.sv'), along with a test duration/rigor tier "
	       "('quick', 'comprehensive', or 'extreme') and optional per-module default 'hierarchical' setting in turbostar.json.";
}

nlohmann::json verilog_configure_testbench_validator::get_parameters_schema() const
{
	return nlohmann::json{
		{"type", "object"},
		{"properties", {
			{"testbench", {
				{"type", "string"},
				{"description", "Path to the testbench .sv/.v file (e.g. 'tb/tb_alu.sv'). Omit to list all configured/auto-discovered testbenches, or when configuring per-module 'hierarchical' default."}
			}},
			{"modules", {
				{"type", "array"},
				{"items", {{"type", "string"}}},
				{"description", "One or more RTL module names or source file paths (e.g. ['alu_mod'] or ['sv/alu/alu.sv']). If omitted when adding a testbench, modules instantiated by the testbench are auto-detected."}
			}},
			{"level", {
				{"type", "string"},
				{"enum", {"quick", "comprehensive", "extreme"}},
				{"description", "Test tier for filtering in verilog_simulate: 'quick' (fast unit/smoke tests), 'comprehensive' (thorough tests), or 'extreme' (exhaustive/long-running tests). Default: 'quick'."}
			}},
			{"hierarchical", {
				{"type", "boolean"},
				{"description", "Optional per-module default for 'hierarchical' in verilog_simulate. When set alongside 'modules'/'module', persists whether verilog_simulate(module=...) automatically includes submodule testbenches by default in turbostar.json."}
			}},
			{"remove", {
				{"type", "boolean"},
				{"description", "When true, removes the specified module association(s) from the testbench, or removes the testbench configuration entirely if 'modules' is omitted (default: false)."}
			}}
		}},
		{"required", nlohmann::json::array()}
	};
}

std::unordered_map<std::string, std::string> verilog_configure_testbench_validator::get_custom_parameter_aliases() const
{
	return {
		{"tb", "testbench"},
		{"testbench_file", "testbench"},
		{"file", "testbench"},
		{"path", "testbench"},
		{"module", "modules"},
		{"rtl_module", "modules"},
		{"tier", "level"},
		{"delete", "remove"},
		{"clear", "remove"}
	};
}

bool verilog_configure_testbench_validator::validate_args_impl(const nlohmann::json &untrusted_args,
                                                               const agentlib::tool_context &ctx,
                                                               std::string &out_error) const
{
	args_ = verilog_configure_testbench_args{};

	if (untrusted_args.contains("remove") && untrusted_args["remove"].is_boolean()) {
		args_.remove = untrusted_args["remove"].get<bool>();
	}

	if (untrusted_args.contains("testbench") && untrusted_args["testbench"].is_string()) {
		std::string untrusted_tb = untrusted_args["testbench"].get<std::string>();
		if (!untrusted_tb.empty()) {
			if (!fs_utils::is_safe_for_ui(untrusted_tb)) {
				out_error = "Invalid characters in 'testbench' parameter";
				return false;
			}
			args_.requested_testbench = untrusted_tb;
			std::string safe_tb;
			if (!ctx.fs_security.validate_access(untrusted_tb, agentlib::access_type::read, safe_tb, out_error)) {
				return false;
			}
			if (!args_.remove && !fs_utils::is_regular_file(safe_tb)) {
				out_error = "Testbench path is not a regular file: " + untrusted_tb;
				return false;
			}
			args_.safe_testbench = safe_tb;
		}
	}

	auto add_req_module = [&](const std::string &untrusted_m) -> bool {
		if (untrusted_m.empty()) {
			return true;
		}
		if (!fs_utils::is_safe_for_ui(untrusted_m)) {
			out_error = "Invalid characters in 'modules' parameter";
			return false;
		}
		args_.requested_modules.push_back(untrusted_m);
		return true;
	};

	if (untrusted_args.contains("modules")) {
		const auto &untrusted_mods = untrusted_args["modules"];
		if (untrusted_mods.is_string()) {
			if (!add_req_module(untrusted_mods.get<std::string>())) {
				return false;
			}
		} else if (untrusted_mods.is_array()) {
			for (const auto &item : untrusted_mods) {
				if (!item.is_string()) {
					out_error = "All elements in 'modules' must be strings";
					return false;
				}
				if (!add_req_module(item.get<std::string>())) {
					return false;
				}
			}
		}
	}

	if (untrusted_args.contains("level") && untrusted_args["level"].is_string()) {
		std::string untrusted_lvl = untrusted_args["level"].get<std::string>();
		if (!untrusted_lvl.empty()) {
			std::string norm = verilog::fpga_project_settings::normalize_test_level(untrusted_lvl);
			if (norm.empty()) {
				out_error = "Invalid 'level' parameter: must be 'quick', 'comprehensive', or 'extreme'";
				return false;
			}
			args_.level = norm;
		}
	}

	if (untrusted_args.contains("hierarchical") && untrusted_args["hierarchical"].is_boolean()) {
		args_.hierarchical = untrusted_args["hierarchical"].get<bool>();
	}

	if (args_.hierarchical.has_value() && args_.requested_modules.empty() && args_.requested_testbench.empty()) {
		out_error = "Parameter 'modules' (or 'module') is required when configuring 'hierarchical'";
		return false;
	}

	if (args_.requested_testbench.empty() && !args_.remove && !args_.hierarchical.has_value() &&
	    (!args_.requested_modules.empty() || !args_.level.empty())) {
		out_error = "Parameter 'testbench' is required when configuring module associations or test level (or pass 'hierarchical' to configure a module's default hierarchy setting)";
		return false;
	}

	// If modifying configuration, verify write access to turbostar.json in workspace root
	if (!args_.requested_testbench.empty() || args_.remove || args_.hierarchical.has_value()) {
		std::string root_dir = ctx.fs_security.get_working_directory().string();
		if (root_dir.empty()) {
			root_dir = ".";
		}
		std::filesystem::path cfg_path = std::filesystem::path(root_dir) / "turbostar.json";
		std::string canon_cfg;
		if (!ctx.fs_security.validate_access(cfg_path.string(), agentlib::access_type::write, canon_cfg, out_error)) {
			return false;
		}
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> verilog_configure_testbench_validator::create_tool_impl(const nlohmann::json &/*untrusted_args*/) const
{
	return std::make_unique<verilog_configure_testbench_tool>(args_);
}

} // namespace tools

extern "C" {

void register_verilog_configure_testbench(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::verilog_configure_testbench_validator>(); }
	);
}

void unregister_verilog_configure_testbench(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("verilog_configure_testbench");
}

}
