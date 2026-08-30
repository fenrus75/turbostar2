#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "config_manager.h"
#include "run_python.h"

namespace tools
{

struct run_python_raw_args {
	std::optional<std::string> code;
	std::optional<std::string> path;
	std::optional<std::vector<std::string>> dependencies;
	std::optional<std::string> venv;
	std::optional<int> timeout;
};

void from_json(const nlohmann::json &j, run_python_raw_args &p)
{
	if (j.contains("code"))
		p.code = j.at("code").get<std::string>();
	if (j.contains("path"))
		p.path = j.at("path").get<std::string>();
	if (j.contains("dependencies"))
		p.dependencies = j.at("dependencies").get<std::vector<std::string>>();
	if (j.contains("venv"))
		p.venv = j.at("venv").get<std::string>();
	if (j.contains("timeout"))
		p.timeout = j.at("timeout").get<int>();
}

class run_python_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	}

	std::string get_name() const override
	{
		return "run_python";
	}
	std::string get_description() const override
	{
		std::string base_desc =
		    "Executes Python code in a sandboxed environment. You MUST use print() statements to see output, as the script runs headlessly. Provide either "
		    "'code' (direct execution) OR 'path' (to run an existing script file).";
		if (config_manager::get_instance().is_allow_code_execution_network()) {
			base_desc += " Script execution has network access enabled.";
		} else {
			base_desc += " Script execution is strictly offline without network access.";
		}
		if (has_uv()) {
			return base_desc + " Optionally provide an array of PyPI 'dependencies' to be temporarily installed via 'uv'.";
		}
		return base_desc;
	}

	nlohmann::json get_parameters_schema() const override
	{
		nlohmann::json props;
		props["code"] = {{"type", "string"}, {"description", "The raw Python code string to execute."}};
		props["path"] = {{"type", "string"}, {"description", "Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt'). Optional relative path to a Python script to execute."}};
		props["timeout"] = {{"type", "integer"}, {"description", "Optional timeout in seconds. Default is 300."}};
		props["venv"] = {{"type", "string"}, {"description", "Optional path to a Python virtual environment directory (e.g. '.venv'). Its interpreter (<venv>/bin/python) is used to run the script, and any 'dependencies' are installed into it. The path is resolved relative to the project root."}};

		if (has_uv()) {
			props["dependencies"] = {
			    {"type", "array"},
			    {"items", {{"type", "string"}}},
			    {"description", "A list of PyPI packages required by the script (e.g., ['requests', 'beautifulsoup4'])."}};
		}

		return {{"type", "object"}, {"properties", props}};
	}

	std::vector<agentlib::tool_example> get_examples() const override
	{
		return {
			{
				"Inline Python Script Execution",
				nlohmann::json{{"code", "import sys, json\ndata = {'status': 'ok', 'count': 42}\nprint(json.dumps(data))\n"}},
				"Executes inline Python code string headlessly. Must use print() to output results."
			},
			{
				"VFS Python Script Execution",
				nlohmann::json{{"path", "tmp://probe.py"}, {"timeout", 30}},
				"Executes Python script stored in temporary VFS location (tmp://probe.py) with 30s timeout."
			},
			{
				"Python Script with PyPI Dependencies",
				nlohmann::json{
					{"code", "import requests\nresp = requests.get('https://httpbin.org/json')\nprint(resp.status_code, len(resp.text))\n"},
					{"dependencies", nlohmann::json::array({"requests"})}
				},
				"Executes Python code with on-demand PyPI dependency installation via uv."
			}
		};
	}

      private:

	bool has_uv() const
	{
		static bool uv_checked = false;
		static bool uv_available = false;
		if (!uv_checked) {
			uv_available = (access("/usr/bin/uv", X_OK) == 0 || access("/usr/local/bin/uv", X_OK) == 0);
			uv_checked = true;
		}
		return uv_available;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override
	{
		try {
			run_python_raw_args raw_args = args_json.get<run_python_raw_args>();

			if (!raw_args.code.has_value() && !raw_args.path.has_value()) {
				out_error = "Must provide exactly one of 'code' or 'path'.";
				return false;
			}
			if (raw_args.code.has_value() && raw_args.path.has_value()) {
				out_error = "Cannot provide both 'code' and 'path'. Choose one.";
				return false;
			}

			args_.code = raw_args.code;
			args_.file_path = raw_args.path;
			if (raw_args.dependencies.has_value()) {
				args_.dependencies = raw_args.dependencies.value();
			}
			if (raw_args.timeout.has_value()) {
				args_.timeout = raw_args.timeout.value();
			} else {
				args_.timeout = 300;
			}
			if (raw_args.venv.has_value() && !raw_args.venv->empty()) {
				std::string resolved_venv;
				if (!ctx.fs_security.validate_access(*raw_args.venv, agentlib::access_type::read, resolved_venv, out_error)) {
					return false;
				}
				args_.venv_dir = resolved_venv;
			}
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<run_python_tool>(args_);
	}

      private:
	mutable run_python_args args_;
};

REGISTER_TOOL(run_python_validator)

} // namespace tools
