#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include "agentlib/interactions/action.h"
#include "agentlib/virtual_file_system.h"

namespace binary_utils {

// Resolves input_data (path, data uri, hex, base64, or VFS uri) into raw bytes
// offset and length can be used to slice the data
std::vector<uint8_t> resolve_input_data(const std::string& input_data, size_t offset = 0, long long length = -1, agentlib::virtual_file_system* vfs = nullptr);

// Resolves input_file (path or VFS uri) into raw bytes
std::vector<uint8_t> resolve_input_file(const std::string& input_file, size_t offset = 0, long long length = -1, agentlib::virtual_file_system* vfs = nullptr);

// Compresses data using the specified format ("zlib", "gzip", "zstd", "xz", "bzip2", "lz4")
std::vector<uint8_t> compress_data(const std::vector<uint8_t>& input, const std::string& format);

// Decompresses data using the specified format (or "auto" for magic byte detection)
std::vector<uint8_t> decompress_data(const std::vector<uint8_t>& input, const std::string& format);

// Formats binary output based on requested output_format ("text", "hex", "base64", "file"), output_file name (if writing to file)
std::string format_binary_output(const std::vector<uint8_t>& data, const std::string& output_format, const std::string& output_file = "");

} // namespace binary_utils
