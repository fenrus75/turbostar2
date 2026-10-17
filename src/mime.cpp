#include "mime.h"
#include "tools/magic_compat.h"
#include "utf8.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>

namespace mime
{

// Looks up canonical MIME type based on file extension (case-insensitive).
// Accepts either full filenames ("document.pdf") or bare extensions ("pdf").
// Returns "application/octet-stream" if the extension is unknown or missing.
std::string from_extension(std::string_view filename_or_ext)
{
	size_t dot = filename_or_ext.find_last_of('.');
	std::string_view ext = (dot == std::string_view::npos) ? filename_or_ext : filename_or_ext.substr(dot + 1);

	auto iequals = [ext](std::string_view target) noexcept {
		if (ext.size() != target.size())
			return false;
		for (size_t i = 0; i < ext.size(); ++i) {
			if (utf8::to_lower(ext[i]) != target[i])
				return false;
		}
		return true;
	};

	if (iequals("png")) return "image/png";
	if (iequals("jpg") || iequals("jpeg")) return "image/jpeg";
	if (iequals("gif")) return "image/gif";
	if (iequals("webp")) return "image/webp";
	if (iequals("pdf")) return "application/pdf";
	if (iequals("txt")) return "text/plain";
	if (iequals("html") || iequals("htm")) return "text/html";
	if (iequals("css")) return "text/css";
	if (iequals("js")) return "text/javascript";
	if (iequals("json")) return "application/json";
	if (iequals("xml")) return "application/xml";
	if (iequals("zip")) return "application/zip";
	if (iequals("tar")) return "application/x-tar";
	if (iequals("gz")) return "application/gzip";
	if (iequals("mp3")) return "audio/mpeg";
	if (iequals("mp4")) return "video/mp4";
	if (iequals("wav")) return "audio/wav";
	if (iequals("ogg")) return "audio/ogg";
	return "application/octet-stream";
}

// Detects Markdown code block language tag from a file path or extension.
// Strips directory paths and leading dots, and lowercases the extension.
// Returns canonical language tag (e.g. "cpp", "python", "javascript", "rust", "go", "csharp", etc.)
// or empty string if unknown.
std::string get_language_from_extension(std::string_view path)
{
	std::filesystem::path p(path);
	std::string ext = p.extension().string();
	if (ext.empty() && !path.empty()) {
		if (path.find('/') == std::string_view::npos && path.find('\\') == std::string_view::npos) {
			ext = std::string(path);
		}
	}
	if (!ext.empty() && ext.front() == '.') {
		ext.erase(0, 1);
	}
	ext = utf8::to_lower(ext);

	if (ext == "cpp" || ext == "cxx" || ext == "cc" || ext == "c" || ext == "h" || ext == "hpp" || ext == "hh" || ext == "hxx") {
		return "cpp";
	}
	if (ext == "py" || ext == "pyw" || ext == "pyi") {
		return "python";
	}
	if (ext == "json" || ext == "jsonc") {
		return "json";
	}
	if (ext == "md" || ext == "markdown") {
		return "markdown";
	}
	if (ext == "sh" || ext == "bash" || ext == "zsh") {
		return "bash";
	}
	if (ext == "js" || ext == "ts" || ext == "jsx" || ext == "tsx" || ext == "cjs" || ext == "mjs") {
		return "javascript";
	}
	if (ext == "html" || ext == "htm") {
		return "html";
	}
	if (ext == "css" || ext == "scss" || ext == "sass" || ext == "less") {
		return "css";
	}
	if (ext == "yaml" || ext == "yml") {
		return "yaml";
	}
	if (ext == "xml" || ext == "svg") {
		return "xml";
	}
	if (ext == "rs") {
		return "rust";
	}
	if (ext == "go") {
		return "go";
	}
	if (ext == "java") {
		return "java";
	}
	if (ext == "cs") {
		return "csharp";
	}
	if (ext == "rb") {
		return "ruby";
	}
	if (ext == "php") {
		return "php";
	}
	if (ext == "sql") {
		return "sql";
	}
	if (ext == "toml" || ext == "ini" || ext == "conf") {
		return "ini";
	}
	if (ext == "lua") {
		return "lua";
	}
	if (ext == "sv" || ext == "svh") {
		return "systemverilog";
	}
	if (ext == "v" || ext == "vh") {
		return "verilog";
	}
	return "";
}

#ifdef HAS_LIBMAGIC
namespace
{
struct magic_cleanup {
	void operator()(magic_t m) const noexcept
	{
		if (m) {
			magic_close(m);
		}
	}
};
using scoped_magic = std::unique_ptr<struct magic_set, magic_cleanup>;
} // namespace
#endif

// Detects the MIME type of a file on disk.
// 1. If path is a URI ("scheme://"), skips disk inspection and falls back to extension.
// 2. Inspects binary signature bytes for common formats via detect_buffer_type.
// 3. Uses libmagic (magic_file with MAGIC_MIME_TYPE) if file exists on disk and libmagic is enabled.
// 4. Fallback: inspects filename extension via from_extension.
std::string detect_file_type(std::string_view path)
{
	std::string path_str(path);
	if (path.find("://") != std::string_view::npos) {
		return from_extension(path);
	}

	// 1. Inspect initial bytes for known binary signatures
	std::ifstream file(path_str, std::ios::binary);
	if (file) {
		char sig[8] = {0};
		file.read(sig, 8);
		std::streamsize bytes_read = file.gcount();
		if (bytes_read >= 4) {
			std::string sig_mime = detect_buffer_type(std::string_view(sig, bytes_read));
			if (sig_mime != "application/octet-stream" && sig_mime != "application/x-empty" && sig_mime != "text/plain") {
				return sig_mime;
			}
		}
	}

#ifdef HAS_LIBMAGIC
	if (std::filesystem::exists(path_str)) {
		scoped_magic magic(magic_open(MAGIC_MIME_TYPE));
		if (magic && magic_load(magic.get(), nullptr) == 0) {
			if (const char *mime = magic_file(magic.get(), path_str.c_str())) {
				std::string res(mime);
				if (res != "application/octet-stream") {
					return res;
				}
			}
		}
	}
#endif

	return from_extension(path);
}

// Detects the MIME type of an in-memory data buffer.
// 1. Inspects binary signature bytes for common image formats (PNG, JPEG, GIF).
// 2. Uses libmagic (magic_buffer with MAGIC_MIME_TYPE) if available.
// 3. Returns "application/octet-stream" if type cannot be determined.
std::string detect_buffer_type(std::string_view buffer)
{
	(void)buffer;

	// Fast signature check for common images
	if (buffer.size() >= 4) {
		const unsigned char *sig = reinterpret_cast<const unsigned char *>(buffer.data());
		if (sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G') {
			return "image/png";
		}
		if (sig[0] == 0xff && sig[1] == 0xd8) {
			return "image/jpeg";
		}
		if (sig[0] == 'G' && sig[1] == 'I' && sig[2] == 'F') {
			return "image/gif";
		}
	}

#ifdef HAS_LIBMAGIC
	scoped_magic magic(magic_open(MAGIC_MIME_TYPE));
	if (magic && magic_load(magic.get(), nullptr) == 0) {
		if (const char *mime = magic_buffer(magic.get(), buffer.data(), buffer.size())) {
			std::string res(mime);
			return res;
		}
	}
#endif

	return "application/octet-stream";
}

// Detects a human-readable description of a file on disk (e.g. "PDF document, version 1.4").
// Uses libmagic (magic_file with MAGIC_NONE) if available. Returns "Unknown file type" on failure.
std::string detect_file_description([[maybe_unused]] std::string_view path)
{
	std::string path_str(path);
#ifdef HAS_LIBMAGIC
	if (std::filesystem::exists(path_str)) {
		scoped_magic magic(magic_open(MAGIC_NONE));
		if (magic && magic_load(magic.get(), nullptr) == 0) {
			if (const char *desc = magic_file(magic.get(), path_str.c_str())) {
				return desc;
			}
		}
	}
#endif
	return "Unknown file type";
}

// Detects a human-readable description of an in-memory buffer.
// Uses libmagic (magic_buffer with MAGIC_NONE) if available. Returns "Unknown file type" on failure.
std::string detect_buffer_description([[maybe_unused]] std::string_view buffer)
{
#ifdef HAS_LIBMAGIC
	scoped_magic magic(magic_open(MAGIC_NONE));
	if (magic && magic_load(magic.get(), nullptr) == 0) {
		if (const char *desc = magic_buffer(magic.get(), buffer.data(), buffer.size())) {
			return desc;
		}
	}
#endif
	return "Unknown file type";
}

// Determines if a source file or extension uses C-style curly brace '{' '}' blocks.
// Accepts full file paths ("src/main.cpp"), dot-extensions (".cpp"), or bare extensions ("cpp").
// Returns true for languages using curly braces (C, C++, C#, Java, Rust, Go, JS, TS, PHP, Swift, Kotlin, CSS, JSON, etc.),
// and false for indentation- or keyword-based languages (Python, Bash, Ruby, YAML, TOML, Markdown, etc.).
bool uses_brace_syntax(std::string_view path_or_ext)
{
	std::string lang = get_language_from_extension(path_or_ext);
	if (lang == "cpp" || lang == "javascript" || lang == "rust" || lang == "go" ||
	    lang == "java" || lang == "csharp" || lang == "css" || lang == "json" ||
	    lang == "xml" || lang == "html" || lang == "php" || lang == "verilog" ||
	    lang == "systemverilog") {
		return true;
	}

	std::filesystem::path p(path_or_ext);
	std::string ext = p.extension().string();
	if (ext.empty() && !path_or_ext.empty()) {
		if (path_or_ext.find('/') == std::string_view::npos && path_or_ext.find('\\') == std::string_view::npos) {
			ext = std::string(path_or_ext);
		}
	}
	if (!ext.empty() && ext.front() == '.') {
		ext.erase(0, 1);
	}
	ext = utf8::to_lower(ext);

	return (ext == "cs" || ext == "php" || ext == "kt" || ext == "kts" ||
		ext == "swift" || ext == "scala" || ext == "d" || ext == "glsl" ||
		ext == "hlsl" || ext == "verilog" || ext == "v" || ext == "vh" ||
		ext == "sv" || ext == "svh" || ext == "systemverilog" || ext == "jsonc");
}

} // namespace mime
