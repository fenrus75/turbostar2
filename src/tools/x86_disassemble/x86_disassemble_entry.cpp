#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>
#include "../../fs_utils.h"
#include "x86_disassemble.h"

#ifdef HAVE_ZYDIS
#include <Zydis/Zydis.h>
#endif

namespace tools
{

static bool parse_ascii_hex(const std::string &input, std::vector<uint8_t> &out_bytes, std::string &out_error)
{
	std::vector<uint8_t> bytes;
	std::string token;
	std::istringstream iss(input);
	std::vector<std::string> tokens;
	while (iss >> token) {
		if (!token.empty() && token.back() == ',') {
			token.pop_back();
		}
		if (!token.empty()) {
			tokens.push_back(token);
		}
	}

	if (tokens.empty()) {
		out_error = "Input hex string is empty.";
		return false;
	}

	// If there is only one token, it is longer than 2 characters, has even length,
	// and consists entirely of hex digits, treat it as contiguous hex.
	if (tokens.size() == 1 && tokens[0].size() > 2 && tokens[0].size() % 2 == 0) {
		std::string t = tokens[0];
		bool all_hex = true;
		for (char c : t) {
			if (!std::isxdigit(static_cast<unsigned char>(c))) {
				all_hex = false;
				break;
			}
		}
		if (all_hex) {
			for (size_t i = 0; i < t.size(); i += 2) {
				std::string byte_str = t.substr(i, 2);
				bytes.push_back(static_cast<uint8_t>(std::strtol(byte_str.c_str(), nullptr, 16)));
			}
			out_bytes = std::move(bytes);
			return true;
		}
	}

	for (const auto &tok : tokens) {
		if (tok.size() > 2) {
			out_error = "Invalid hex token: \"" + tok + "\" (too long for a single byte)";
			return false;
		}
		char *endptr = nullptr;
		long val = std::strtol(tok.c_str(), &endptr, 16);
		if (*endptr != '\0' || val < 0 || val > 255) {
			out_error = "Invalid hex token: \"" + tok + "\"";
			return false;
		}
		bytes.push_back(static_cast<uint8_t>(val));
	}
	out_bytes = std::move(bytes);
	return true;
}

static bool parse_base64(const std::string &input, std::vector<uint8_t> &out_bytes, std::string &out_error)
{
	std::string stripped;
	stripped.reserve(input.size());
	for (char c : input) {
		if (!std::isspace(static_cast<unsigned char>(c))) {
			stripped.push_back(c);
		}
	}

	for (char c : stripped) {
		bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '/' || c == '=';
		if (!ok) {
			out_error = "Invalid character in Base64 input.";
			return false;
		}
	}

	auto decoded = fs_utils::base64_decode(stripped);
	if (decoded.empty() && !stripped.empty()) {
		out_error = "Failed to decode Base64 input.";
		return false;
	}

	out_bytes.assign(decoded.begin(), decoded.end());
	return true;
}

x86_disassemble_tool::x86_disassemble_tool(x86_disassemble_args args)
    : llm_tool_action("Disassembling x86 machine code"), args_(std::move(args))
{
}

bool x86_disassemble_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string x86_disassemble_tool::execute(agentlib::tool_context &ctx)
{
	std::vector<uint8_t> bytes;
	std::string parse_error;
	bool parsed_ok = false;

	if (args_.format == "hex") {
		parsed_ok = parse_ascii_hex(args_.data, bytes, parse_error);
	} else if (args_.format == "base64") {
		parsed_ok = parse_base64(args_.data, bytes, parse_error);
	} else { // "auto"
		parsed_ok = parse_ascii_hex(args_.data, bytes, parse_error);
		if (!parsed_ok) {
			std::string b64_error;
			if (parse_base64(args_.data, bytes, b64_error)) {
				parsed_ok = true;
			} else {
				parse_error = "Could not parse input as hex (" + parse_error + ") or base64 (" + b64_error + ").";
			}
		}
	}

	if (!parsed_ok) {
		set_failure(ctx, parse_error);
		return "Error parsing input: " + parse_error;
	}

#ifndef HAVE_ZYDIS
	set_failure(ctx, "Zydis support is not enabled in this build.");
	return "Error: Zydis disassembly library is not available in this build of Turbostar.";
#else
	ZydisMachineMode machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
	if (args_.mode == "32") {
		machine_mode = ZYDIS_MACHINE_MODE_LEGACY_32;
	} else if (args_.mode == "16") {
		machine_mode = ZYDIS_MACHINE_MODE_LEGACY_16;
	}

	std::string result = "| Bytes | Instruction |\n| --- | --- |\n";
	size_t offset = 0;

	while (offset < bytes.size()) {
		ZydisDisassembledInstruction instruction;
		ZyanStatus status;
		if (args_.syntax == "att") {
			status = ZydisDisassembleATT(machine_mode, args_.address + offset, bytes.data() + offset, bytes.size() - offset,
						     &instruction);
		} else {
			status = ZydisDisassembleIntel(machine_mode, args_.address + offset, bytes.data() + offset, bytes.size() - offset,
						       &instruction);
		}

		size_t length = 0;
		std::string instr_text;
		if (ZYAN_SUCCESS(status)) {
			length = instruction.info.length;
			instr_text = instruction.text;
		} else {
			length = 1;
			instr_text = std::format("db 0x{:02x}", bytes[offset]);
		}

		std::string bytes_str;
		for (size_t i = 0; i < length; ++i) {
			if (i > 0) {
				bytes_str += " ";
			}
			bytes_str += std::format("{:02x}", bytes[offset + i]);
		}

		std::string escaped_instr = instr_text;
		size_t pipe_pos = 0;
		while ((pipe_pos = escaped_instr.find('|', pipe_pos)) != std::string::npos) {
			escaped_instr.replace(pipe_pos, 1, "\\|");
			pipe_pos += 2;
		}

		result += std::format("| {} | {} |\n", bytes_str, escaped_instr);
		offset += length;
	}

	set_success(ctx, "Disassembled " + std::to_string(bytes.size()) + " bytes");
	return result;
#endif
}

} // namespace tools
