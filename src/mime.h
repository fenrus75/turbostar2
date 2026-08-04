#pragma once

#include <string>
#include <string_view>

namespace mime
{

/**
 * @brief Detects the MIME type of a file on disk.
 * Uses libmagic (via magic_file) if available, falling back to extension detection.
 */
std::string detect_file_type(std::string_view path);

/**
 * @brief Detects the MIME type of a raw memory buffer.
 * Uses libmagic (via magic_buffer) if available, falling back to signature bytes or generic default.
 */
std::string detect_buffer_type(std::string_view buffer);

/**
 * @brief Detects a human-readable description of the file type from a raw memory buffer.
 * Uses libmagic (via magic_buffer with MAGIC_NONE) if available.
 */
std::string detect_buffer_description(std::string_view buffer);

/**
 * @brief Looks up a MIME type by file extension (case-insensitive).
 */
std::string from_extension(std::string_view filename_or_ext);

/**
 * @brief Detects Markdown syntax highlighting language identifier from file path or extension.
 * Returns language string (e.g., "cpp", "python", "javascript", "markdown", etc.) or empty string if unknown.
 */
std::string get_language_from_extension(std::string_view path);

/**
 * @brief Detects a human-readable description of the file type (e.g. "ELF 64-bit LSB shared object").
 * Uses libmagic (via magic_file with MAGIC_NONE) if available.
 */
std::string detect_file_description(std::string_view path);

} // namespace mime
