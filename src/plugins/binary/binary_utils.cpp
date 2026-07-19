#include "binary_utils.h"
#include "fs_utils.h"
#include "mime.h"
#include "utf8.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <zlib.h>
#include <cstring>

#ifdef HAS_LIBZSTD
#include <zstd.h>
#endif

#ifdef HAS_LIBLZMA
#include <lzma.h>
#endif

#ifdef HAS_BZIP2
#include <bzlib.h>
#endif

#ifdef HAS_LIBLZ4
#include <lz4.h>
#endif

namespace binary_utils {

static std::vector<uint8_t> read_file(const std::string& path, size_t offset, long long length) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + path);
    }
    file.seekg(offset, std::ios::beg);
    if (length < 0) {
        file.seekg(0, std::ios::end);
        length = (long long)file.tellg() - (long long)offset;
        file.seekg(offset, std::ios::beg);
    }
    if (length <= 0) return {};
    std::vector<uint8_t> data(length);
    file.read(reinterpret_cast<char*>(data.data()), length);
    data.resize(file.gcount());
    return data;
}

std::vector<uint8_t> resolve_input_data(const std::string& input_data, size_t offset, long long length, agentlib::virtual_file_system* vfs) {
    if (input_data.starts_with("data:")) {
        size_t comma = input_data.find(',');
        if (comma != std::string::npos) {
            std::string b64 = input_data.substr(comma + 1);
            auto decoded = fs_utils::base64_decode(b64);
            if (length < 0) length = decoded.size() - offset;
            if (offset >= decoded.size() || length <= 0) return {};
            size_t actual_len = std::min((size_t)length, decoded.size() - offset);
            return std::vector<uint8_t>(decoded.begin() + offset, decoded.begin() + offset + actual_len);
        }
    }

    // Try VFS if it's a URI
    if (vfs && input_data.find("://") != std::string::npos) {
        if (vfs->exists(input_data)) {
            auto handle = vfs->read_file(input_data);
            if (handle) {
                auto view = (*handle)->view();
                if (length < 0) length = view.size() - offset;
                if (offset >= view.size() || length <= 0) return {};
                size_t actual_len = std::min((size_t)length, view.size() - offset);
                return std::vector<uint8_t>(view.begin() + offset, view.begin() + offset + actual_len);
            }
        }
    }
    
    // Check if it's a file on local disk
    try {
        if (std::filesystem::exists(fs_utils::safe_absolute(input_data))) {
            return read_file(fs_utils::safe_absolute(input_data).string(), offset, length);
        }
    } catch (...) {}

    // Try hex (must be all hex and even length)
    bool is_hex = true;
    std::string clean_hex;
    for (char c : input_data) {
        if (std::isspace(c)) continue;
        if (!std::isxdigit(c)) {
            is_hex = false;
            break;
        }
        clean_hex += c;
    }
    if (is_hex && clean_hex.length() % 2 == 0 && !clean_hex.empty()) {
        std::vector<uint8_t> result;
        for (size_t i = 0; i < clean_hex.length(); i += 2) {
            result.push_back((uint8_t)std::strtol(clean_hex.substr(i, 2).c_str(), nullptr, 16));
        }
        if (length < 0) length = result.size() - offset;
        if (offset >= result.size() || length <= 0) return {};
        size_t actual_len = std::min((size_t)length, result.size() - offset);
        return std::vector<uint8_t>(result.begin() + offset, result.begin() + offset + actual_len);
    }

    // Try base64
    try {
        auto decoded = fs_utils::base64_decode(input_data);
        if (length < 0) length = decoded.size() - offset;
        if (offset >= decoded.size() || length <= 0) return {};
        size_t actual_len = std::min((size_t)length, decoded.size() - offset);
        return std::vector<uint8_t>(decoded.begin() + offset, decoded.begin() + offset + actual_len);
    } catch (...) {}

    // Fallback: literal string
    std::vector<uint8_t> result(input_data.begin(), input_data.end());
    if (length < 0) length = result.size() - offset;
    if (offset >= result.size() || length <= 0) return {};
    size_t actual_len = std::min((size_t)length, result.size() - offset);
    return std::vector<uint8_t>(result.begin() + offset, result.begin() + offset + actual_len);
}

std::vector<uint8_t> compress_data(const std::vector<uint8_t>& input, const std::string& format) {
    std::string fmt = format;
    if (fmt == "deflate") fmt = "zlib";

    if (fmt == "zlib" || fmt == "gzip") {
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        int windowBits = format == "gzip" ? 15 + 16 : 15;
        if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            throw std::runtime_error("deflateInit2 failed - invalid format?");
        }
        zs.next_in = (Bytef*)input.data();
        zs.avail_in = input.size();
        int ret;
        std::vector<uint8_t> outbuffer(32768);
        std::vector<uint8_t> result;
        do {
            zs.next_out = reinterpret_cast<Bytef*>(outbuffer.data());
            zs.avail_out = outbuffer.size();
            ret = deflate(&zs, Z_FINISH);
            if (result.size() < zs.total_out) {
                result.insert(result.end(), outbuffer.begin(), outbuffer.begin() + (outbuffer.size() - zs.avail_out));
            }
        } while (ret == Z_OK);
        deflateEnd(&zs);
        if (ret != Z_STREAM_END) {
            throw std::runtime_error("deflate failed - invalid format?");
        }
        return result;
    }
#ifdef HAS_LIBZSTD
    if (fmt == "zstd") {
        size_t bound = ZSTD_compressBound(input.size());
        std::vector<uint8_t> result(bound);
        size_t csize = ZSTD_compress(result.data(), bound, input.data(), input.size(), 3);
        if (ZSTD_isError(csize)) {
            throw std::runtime_error("ZSTD_compress failed - invalid format?");
        }
        result.resize(csize);
        return result;
    }
#endif
    throw std::runtime_error("Unsupported or unavailable compression format: " + format);
}

std::vector<uint8_t> decompress_data(const std::vector<uint8_t>& input, const std::string& format) {
    std::string fmt = format;
    if (fmt == "deflate") fmt = "zlib";
    
    if (fmt == "auto") {
        if (input.size() >= 2 && input[0] == 0x1F && input[1] == 0x8B) fmt = "gzip";
        else if (input.size() >= 2 && input[0] == 0x78) fmt = "zlib";
        else if (input.size() >= 4 && input[0] == 0x28 && input[1] == 0xB5 && input[2] == 0x2F && input[3] == 0xFD) fmt = "zstd";
        else if (input.size() >= 6 && input[0] == 0xFD && input[1] == '7' && input[2] == 'z' && input[3] == 'X' && input[4] == 'Z' && input[5] == 0x00) fmt = "xz";
        else if (input.size() >= 3 && input[0] == 'B' && input[1] == 'Z' && input[2] == 'h') fmt = "bzip2";
        else if (input.size() >= 4 && input[0] == 0x04 && input[1] == 0x22 && input[2] == 0x4D && input[3] == 0x18) fmt = "lz4";
        else throw std::runtime_error("Could not auto-detect format from magic bytes");
    }

    if (fmt == "zlib" || fmt == "gzip") {
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        int windowBits = fmt == "gzip" ? 15 + 16 : 15;
        // fallback logic for zlib: sometimes PDFs lack header, we might need windowBits = -15
        if (inflateInit2(&zs, windowBits) != Z_OK) {
            throw std::runtime_error("inflateInit2 failed - invalid format?");
        }
        zs.next_in = (Bytef*)input.data();
        zs.avail_in = input.size();
        int ret;
        std::vector<uint8_t> outbuffer(32768);
        std::vector<uint8_t> result;
        do {
            zs.next_out = reinterpret_cast<Bytef*>(outbuffer.data());
            zs.avail_out = outbuffer.size();
            ret = inflate(&zs, 0);
            if (result.size() < zs.total_out) {
                result.insert(result.end(), outbuffer.begin(), outbuffer.begin() + (outbuffer.size() - zs.avail_out));
            }
        } while (ret == Z_OK);
        inflateEnd(&zs);
        // We accept Z_STREAM_END or Z_BUF_ERROR if we got some output
        if (ret != Z_STREAM_END && result.empty()) {
            // try raw deflate for zlib
            if (fmt == "zlib") {
                memset(&zs, 0, sizeof(zs));
                if (inflateInit2(&zs, -15) == Z_OK) {
                    zs.next_in = (Bytef*)input.data();
                    zs.avail_in = input.size();
                    result.clear();
                    do {
                        zs.next_out = reinterpret_cast<Bytef*>(outbuffer.data());
                        zs.avail_out = outbuffer.size();
                        ret = inflate(&zs, 0);
                        if (result.size() < zs.total_out) {
                            result.insert(result.end(), outbuffer.begin(), outbuffer.begin() + (outbuffer.size() - zs.avail_out));
                        }
                    } while (ret == Z_OK);
                    inflateEnd(&zs);
                }
            }
            if (result.empty()) {
                throw std::runtime_error("inflate failed - invalid format?");
            }
        }
        return result;
    }
#ifdef HAS_LIBZSTD
    if (fmt == "zstd") {
        unsigned long long const rSize = ZSTD_getFrameContentSize(input.data(), input.size());
        if (rSize == ZSTD_CONTENTSIZE_ERROR) throw std::runtime_error("Not compressed by zstd");
        if (rSize == ZSTD_CONTENTSIZE_UNKNOWN) throw std::runtime_error("Original size unknown");
        std::vector<uint8_t> result(rSize);
        size_t dSize = ZSTD_decompress(result.data(), rSize, input.data(), input.size());
        if (ZSTD_isError(dSize)) throw std::runtime_error("ZSTD_decompress failed - invalid format?");
        return result;
    }
#endif
    throw std::runtime_error("Unsupported or unavailable decompression format: " + fmt);
}

std::string format_binary_output(const std::vector<uint8_t>& data, const std::string& output_format, const std::string& output_file) {
    if (!output_file.empty()) {
        std::string path = fs_utils::safe_absolute(output_file).string();
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) throw std::runtime_error("Could not write to output file: " + path);
        ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
        return "Successfully wrote " + std::to_string(data.size()) + " bytes to " + output_file;
    }

    std::string_view data_sv(reinterpret_cast<const char*>(data.data()), data.size());
    if (output_format == "text") {
        if (utf8::is_valid_utf8(data_sv)) {
            return std::string(data.begin(), data.end());
        } else {
            std::string mime_type = mime::detect_buffer_type(data_sv);
            return fs_utils::format_binary_output(data, "base64", mime_type);
        }
    }
    
    std::string mime_type = mime::detect_buffer_type(data_sv);
    return fs_utils::format_binary_output(data, output_format, mime_type);
}

} // namespace binary_utils