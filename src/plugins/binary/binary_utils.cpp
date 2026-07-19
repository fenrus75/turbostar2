#include "binary_utils.h"
#include "fs_utils.h"
#include "mime.h"
#include "utf8.h"
#include "agentlib/virtual_file_system.h"
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

static std::vector<uint8_t> ascii85_decode(const std::vector<uint8_t>& input) {
	std::vector<uint8_t> output;
	size_t start = (input.size() >= 2 && input[0] == '<' && input[1] == '~') ? 2 : 0;
	size_t end = (input.size() >= 2 && input[input.size() - 2] == '~' && input[input.size() - 1] == '>') ? input.size() - 2 : input.size();

	std::vector<uint8_t> clean;
	clean.reserve(end - start);
	for (size_t i = start; i < end; ++i) {
		uint8_t c = input[i];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0') {
			continue;
		}
		clean.push_back(c);
	}

	size_t i = 0;
	while (i < clean.size()) {
		if (clean[i] == 'z') {
			output.insert(output.end(), 4, 0);
			i++;
			continue;
		}
		uint8_t block[5] = {84, 84, 84, 84, 84}; // pad with 'u' (value 84)
		size_t count = 0;
		while (count < 5 && i < clean.size()) {
			uint8_t c = clean[i++];
			if (c >= 33 && c <= 117) {
				block[count++] = c - 33;
			} else if (c == 'z') {
				throw std::runtime_error("ASCII85Decode: 'z' found inside a block");
			} else {
				throw std::runtime_error("ASCII85Decode: invalid character in stream");
			}
		}
		if (count == 0) {
			break;
		}
		if (count == 1) {
			throw std::runtime_error("ASCII85Decode: invalid block size of 1");
		}

		uint64_t sum = 0;
		for (size_t p = 0; p < 5; ++p) {
			sum = sum * 85 + block[p];
		}
		if (sum > 0xFFFFFFFF) {
			throw std::runtime_error("ASCII85Decode: block value overflow");
		}

		uint32_t val = static_cast<uint32_t>(sum);
		uint8_t bytes[4] = {
			static_cast<uint8_t>((val >> 24) & 0xFF),
			static_cast<uint8_t>((val >> 16) & 0xFF),
			static_cast<uint8_t>((val >> 8) & 0xFF),
			static_cast<uint8_t>(val & 0xFF)
		};
		output.insert(output.end(), bytes, bytes + ((count == 5) ? 4 : count - 1));
	}
	return output;
}

static std::vector<uint8_t> run_length_decode(const std::vector<uint8_t>& input) {
	std::vector<uint8_t> output;
	size_t i = 0;
	while (i < input.size()) {
		uint8_t header = input[i++];
		if (header == 128) {
			break;
		} else if (header < 128) {
			size_t count = header + 1;
			if (i + count > input.size()) {
				throw std::runtime_error("RunLengthDecode: unexpected EOF reading literal bytes");
			}
			output.insert(output.end(), input.begin() + i, input.begin() + i + count);
			i += count;
		} else {
			size_t count = 257 - header;
			if (i >= input.size()) {
				throw std::runtime_error("RunLengthDecode: unexpected EOF reading repeated byte");
			}
			uint8_t val = input[i++];
			output.insert(output.end(), count, val);
		}
	}
	return output;
}

class LZWBitReader {
	const std::vector<uint8_t>& data_;
	size_t byte_idx_ = 0;
	size_t bit_idx_ = 0;

public:
	LZWBitReader(const std::vector<uint8_t>& data) : data_(data) {}

	uint32_t read_bits(size_t bits) {
		uint32_t val = 0;
		size_t bits_needed = bits;
		while (bits_needed > 0) {
			if (byte_idx_ >= data_.size()) {
				return 257; // End of Data
			}
			size_t bits_available = 8 - bit_idx_;
			size_t bits_to_take = std::min(bits_needed, bits_available);
			
			uint8_t current_byte = data_[byte_idx_];
			uint8_t mask = (1 << bits_to_take) - 1;
			uint8_t shift = bits_available - bits_to_take;
			uint32_t bits_val = (current_byte >> shift) & mask;
			
			val = (val << bits_to_take) | bits_val;
			
			bits_needed -= bits_to_take;
			bit_idx_ += bits_to_take;
			if (bit_idx_ == 8) {
				byte_idx_++;
				bit_idx_ = 0;
			}
		}
		return val;
	}
};

static std::vector<uint8_t> lzw_decode(const std::vector<uint8_t>& input, int early_change = 1) {
	std::vector<uint8_t> output;
	if (input.empty()) return output;

	LZWBitReader reader(input);
	
	std::vector<std::vector<uint8_t>> table(4096);
	auto reset_table = [&]() {
		for (int i = 0; i < 256; ++i) {
			table[i] = { static_cast<uint8_t>(i) };
		}
	};
	
	reset_table();
	size_t code_size = 9;
	size_t next_code = 258;
	
	uint32_t old_code = reader.read_bits(code_size);
	if (old_code == 257) return output;
	if (old_code == 256) {
		reset_table();
		next_code = 258;
		code_size = 9;
		old_code = reader.read_bits(code_size);
		if (old_code == 257) return output;
	}
	
	output.insert(output.end(), table[old_code].begin(), table[old_code].end());
	
	while (true) {
		uint32_t code = reader.read_bits(code_size);
		if (code == 257) {
			break;
		}
		if (code == 256) {
			reset_table();
			next_code = 258;
			code_size = 9;
			old_code = reader.read_bits(code_size);
			if (old_code == 257) break;
			output.insert(output.end(), table[old_code].begin(), table[old_code].end());
			continue;
		}
		
		std::vector<uint8_t> sequence;
		if (code < next_code) {
			sequence = table[code];
		} else if (code == next_code) {
			sequence = table[old_code];
			sequence.push_back(table[old_code][0]);
		} else {
			throw std::runtime_error("LZWDecode: invalid code sequence");
		}
		
		output.insert(output.end(), sequence.begin(), sequence.end());
		
		if (next_code < 4096) {
			std::vector<uint8_t> new_seq = table[old_code];
			new_seq.push_back(sequence[0]);
			table[next_code++] = new_seq;
		}
		
		size_t limit = (1 << code_size) - (early_change ? 1 : 0);
		if (next_code >= limit && code_size < 12) {
			code_size++;
		}
		
		old_code = code;
	}
	
	return output;
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
        else if (input.size() >= 2 && input[0] == '<' && input[1] == '~') fmt = "ascii85";
        else throw std::runtime_error("Could not auto-detect format from magic bytes");
    }

    if (fmt == "zlib" || fmt == "gzip") {
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        int windowBits = fmt == "gzip" ? 15 + 16 : 15;
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
        if (ret != Z_STREAM_END && result.empty()) {
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

    if (fmt == "pdflzw" || fmt == "lzw") {
        return lzw_decode(input, 1);
    }
    if (fmt == "pdfrunlength" || fmt == "runlength") {
        return run_length_decode(input);
    }
    if (fmt == "ascii85") {
        return ascii85_decode(input);
    }

    throw std::runtime_error("Unsupported or unavailable decompression format: " + fmt);
}

std::string format_binary_output(const std::vector<uint8_t>& data, const std::string& output_format, const std::string& output_file) {
    if (!output_file.empty()) {
        std::string uri = output_file;
        if (uri.find("://") == std::string::npos) {
            uri = "file://" + fs_utils::safe_absolute(output_file).string();
        }

        agentlib::virtual_file_system vfs;
        std::string result = vfs.write_file(uri, data.data(), data.size());
        if (result.empty()) {
            throw std::runtime_error("Could not write output to VFS path: " + output_file);
        }
        return result;
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