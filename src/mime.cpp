#include "mime.h"
#include "tools/magic_compat.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace mime
{

std::string from_extension(const std::string &filename_or_ext)
{
	size_t dot = filename_or_ext.find_last_of('.');
	std::string ext = (dot == std::string::npos) ? filename_or_ext : filename_or_ext.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	if (ext == "png") return "image/png";
	if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
	if (ext == "gif") return "image/gif";
	if (ext == "webp") return "image/webp";
	if (ext == "pdf") return "application/pdf";
	if (ext == "txt") return "text/plain";
	if (ext == "html" || ext == "htm") return "text/html";
	if (ext == "css") return "text/css";
	if (ext == "js") return "text/javascript";
	if (ext == "json") return "application/json";
	if (ext == "xml") return "application/xml";
	if (ext == "zip") return "application/zip";
	if (ext == "tar") return "application/x-tar";
	if (ext == "gz") return "application/gzip";
	if (ext == "mp3") return "audio/mpeg";
	if (ext == "mp4") return "video/mp4";
	if (ext == "wav") return "audio/wav";
	if (ext == "ogg") return "audio/ogg";
	return "application/octet-stream";
}

std::string detect_file_type(const std::string &path)
{
#ifdef HAS_LIBMAGIC
	if (path.find("://") == std::string::npos && std::filesystem::exists(path)) {
		magic_t magic = magic_open(MAGIC_MIME_TYPE);
		if (magic) {
			if (magic_load(magic, nullptr) == 0) {
				const char *mime = magic_file(magic, path.c_str());
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
	std::ifstream file(path, std::ios::binary);
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

std::string detect_file_description([[maybe_unused]] const std::string &path)
{
#ifdef HAS_LIBMAGIC
	if (std::filesystem::exists(path)) {
		magic_t magic = magic_open(MAGIC_NONE);
		if (magic) {
			if (magic_load(magic, nullptr) == 0) {
				const char *desc = magic_file(magic, path.c_str());
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
