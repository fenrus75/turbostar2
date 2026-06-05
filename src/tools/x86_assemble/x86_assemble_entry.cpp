#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include "../../fs_utils.h"
#include "../../project_manager.h"
#include "x86_assemble.h"

namespace tools
{

x86_assemble_tool::x86_assemble_tool(x86_assemble_args args) : llm_tool_action("Assembling x86 instruction"), args_(std::move(args))
{
}

bool x86_assemble_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string x86_assemble_tool::execute(agentlib::tool_context &ctx)
{
	std::string temp_dir = ".";
	std::string project_root = project_manager::get_instance().get_project_root();
	if (!project_root.empty()) {
		temp_dir = project_root + "/build";
	}

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(100000, 999999);
	std::string unique_id = std::to_string(dis(gen));

	std::string s_file = temp_dir + "/ts_asm_" + unique_id + ".s";
	std::string o_file = temp_dir + "/ts_asm_" + unique_id + ".o";
	std::string bin_file = temp_dir + "/ts_asm_" + unique_id + ".bin";

	std::ofstream out(s_file);
	if (!out) {
		set_failure(ctx, "Failed to create temporary assembly source file.");
		return "Error: Failed to create temporary assembly source file.";
	}

	if (args_.mode == "16") {
		out << ".code16\n";
	} else if (args_.mode == "32") {
		out << ".code32\n";
	} else if (args_.mode == "64") {
		out << ".code64\n";
	}

	if (args_.syntax == "intel") {
		out << ".intel_syntax noprefix\n";
	} else {
		out << ".att_syntax prefix\n";
	}

	out << args_.instruction << "\n";
	out.close();

	std::string as_flag = "";
	if (args_.mode == "64") {
		as_flag = "--64";
	} else if (args_.mode == "32") {
		as_flag = "--32";
	}

	// Run GNU Assembler
	std::string as_cmd = std::format("as {} -o {} {}", as_flag, o_file, s_file);
	std::string as_output = fs_utils::execute_command_sync(as_cmd);

	if (!std::filesystem::exists(o_file)) {
		std::filesystem::remove(s_file);
		set_failure(ctx, "Assembly failed: " + as_output);
		return "Error: Assembly failed.\n" + as_output;
	}

	// Run objcopy to extract the raw binary code
	std::string objcopy_cmd = std::format("objcopy -O binary -j .text {} {}", o_file, bin_file);
	std::string objcopy_output = fs_utils::execute_command_sync(objcopy_cmd);

	if (!std::filesystem::exists(bin_file)) {
		std::filesystem::remove(s_file);
		std::filesystem::remove(o_file);
		set_failure(ctx, "Extraction of machine code failed: " + objcopy_output);
		return "Error: Extraction of machine code failed.\n" + objcopy_output;
	}

	// Read binary bytes
	std::ifstream in(bin_file, std::ios::binary);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();

	// Clean up temporary files
	std::filesystem::remove(s_file);
	std::filesystem::remove(o_file);
	std::filesystem::remove(bin_file);

	if (bytes.empty()) {
		set_failure(ctx, "Generated machine code is empty.");
		return "Error: Generated machine code is empty.";
	}

	// Format as space-separated hex bytes
	std::string hex_str;
	for (size_t i = 0; i < bytes.size(); ++i) {
		if (i > 0) {
			hex_str += " ";
		}
		hex_str += std::format("{:02x}", bytes[i]);
	}

	set_success(ctx, "Assembled instruction successfully.");
	return hex_str;
}

} // namespace tools
