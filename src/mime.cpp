#include "mime.h"
#include "tools/magic_compat.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace mime
{

std::string from_extension(std::string_view filename_or_ext)
{
	size_t dot = filename_or_ext.find_last_of('.');
	std::string_view ext = (dot == std::string_view::npos) ? filename_or_ext : filename_or_ext.substr(dot + 1);

	auto iequals = [ext](std::string_view target) noexcept {
		if (ext.size() != target.size())
			return false;
		for (size_t i = 0; i < ext.size(); ++i) {
			if (std::tolower(static_cast<unsigned char>(ext[i])) != target[i])
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
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	});

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
	return "";
}

std::string detect_file_type(std::string_view path)
{
	std::string path_str(path);
#ifdef HAS_LIBMAGIC
	if (path.find("://") == std::string_view::npos && std::filesystem::exists(path_str)) {
		magic_t magic = magic_open(MAGIC_MIME_TYPE);
		if (magic) {
			if (magic_load(magic, nullptr) == 0) {
				const char *mime = magic_file(magic, path_str.c_str());
				if (mime) {
					std::string res(mime);
					magic_close(magic);
					return res;
				}
			}
			magic_close(magic);
		}
	}
#endif
	// Fallback to reading first 8 bytes and checking signatures via detect_buffer_type
	std::ifstream file(path_str, std::ios::binary);
	if (file) {
		char sig[8] = {0};
		file.read(sig, 8);
		std::streamsize bytes_read = file.gcount();
		if (bytes_read > 0) {
			std::string sig_mime = detect_buffer_type(std::string_view(sig, bytes_read));
			if (sig_mime != "application/octet-stream") {
				return sig_mime;
			}
		}
	}

	return from_extension(path);
}

std::string detect_buffer_type(std::string_view buffer)
{
#ifdef HAS_LIBMAGIC
	magic_t magic = magic_open(MAGIC_MIME_TYPE);
	if (magic) {
		if (magic_load(magic, nullptr) == 0) {
			const char *mime = magic_buffer(magic, buffer.data(), buffer.size());
			if (mime) {
				std::string res(mime);
				magic_close(magic);
				return res;
			}
		}
		magic_close(magic);
	}
#endif

	// Fallback to signature bytes for common images
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

	return "application/octet-stream";
}

std::string detect_file_description([[maybe_unused]] std::string_view path)
{
	std::string path_str(path);
#ifdef HAS_LIBMAGIC
	if (std::filesystem::exists(path_str)) {
		magic_t magic = magic_open(MAGIC_NONE);
		if (magic) {
			if (magic_load(magic, nullptr) == 0) {
				const char *desc = magic_file(magic, path_str.c_str());
				if (desc) {
					std::string res(desc);
					magic_close(magic);
					return res;
				}
			}
			magic_close(magic);
		}
	}
#endif
	return "Unknown file type";
}

std::string detect_buffer_description([[maybe_unused]] std::string_view buffer)
{
#ifdef HAS_LIBMAGIC
	magic_t magic = magic_open(MAGIC_NONE);
	if (magic) {
		if (magic_load(magic, nullptr) == 0) {
			const char *desc = magic_buffer(magic, buffer.data(), buffer.size());
			if (desc) {
				std::string res(desc);
				magic_close(magic);
				return res;
			}
		}
		magic_close(magic);
	}
#endif
	return "Unknown file type";
}

} // namespace mime
