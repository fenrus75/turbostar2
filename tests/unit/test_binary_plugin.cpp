#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include "plugins/binary/binary_utils.h"
#include "project_manager.h"
#include "images/image_manager.h"

void test_compress_decompress_roundtrip(const std::string &format) {
    std::string original = "Hello World! This is a test string to be compressed and decompressed with " + format + ". Let's make sure it roundtrips successfully!";
    std::vector<uint8_t> input(original.begin(), original.end());

    std::vector<uint8_t> compressed = binary_utils::compress_data(input, format);
    assert(!compressed.empty());
    assert(compressed != input);

    std::vector<uint8_t> decompressed = binary_utils::decompress_data(compressed, format);
    std::string result(decompressed.begin(), decompressed.end());
    assert(result == original);

    // Test auto-detection during decompression
    std::vector<uint8_t> decompressed_auto = binary_utils::decompress_data(compressed, "auto");
    std::string result_auto(decompressed_auto.begin(), decompressed_auto.end());
    assert(result_auto == original);
    
    std::cout << "test_compress_decompress_roundtrip (" << format << ") passed!" << std::endl;
}

void test_compress_decompress_all() {
    test_compress_decompress_roundtrip("zlib");
    test_compress_decompress_roundtrip("deflate");
    test_compress_decompress_roundtrip("gzip");
#ifdef HAS_LIBZSTD
    test_compress_decompress_roundtrip("zstd");
#endif
}

void test_resolve_input_data_base64() {
    std::string base64 = "SGVsbG8="; // "Hello"
    std::vector<uint8_t> resolved = binary_utils::resolve_input_data(base64, 0, -1);
    std::string result(resolved.begin(), resolved.end());
    assert(result == "Hello");
    std::cout << "test_resolve_input_data_base64 passed!" << std::endl;
}

void test_resolve_input_data_hex() {
    std::string hex = "48656c6c6f"; // "Hello"
    std::vector<uint8_t> resolved = binary_utils::resolve_input_data(hex, 0, -1);
    std::string result(resolved.begin(), resolved.end());
    assert(result == "Hello");
    std::cout << "test_resolve_input_data_hex passed!" << std::endl;
}

void test_format_binary_output_base64_fallback() {
    // Valid text string
    std::vector<uint8_t> valid_text = {'H', 'e', 'l', 'l', 'o'};
    std::string text_out = binary_utils::format_binary_output(valid_text, "text", "");
    assert(text_out == "Hello");

    // Invalid UTF-8 bytes (non-ASCII, invalid lead byte)
    std::vector<uint8_t> invalid_utf8 = {0xFF, 0x00, 0x12};
    std::string binary_out = binary_utils::format_binary_output(invalid_utf8, "text", "");
    assert(binary_out.find("data:application/octet-stream;base64,") == 0);
    // Base64 of {0xFF, 0x00, 0x12} is "/wAS"
    assert(binary_out == "data:application/octet-stream;base64,/wAS");

    std::cout << "test_format_binary_output_base64_fallback passed!" << std::endl;
}

void test_format_binary_output_mime_detection() {
    // Mock PNG signature + IHDR chunk (33 bytes)
    std::vector<uint8_t> png_data = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, // Signature
        0x00, 0x00, 0x00, 0x0d,                         // IHDR Length
        'I', 'H', 'D', 'R',                             // IHDR Type
        0x00, 0x00, 0x00, 0x0a,                         // Width (10)
        0x00, 0x00, 0x00, 0x05,                         // Height (5)
        0x08, 0x02, 0x00, 0x00, 0x00,                   // Extra
        0x00, 0x00, 0x00, 0x00                          // CRC
    };
    std::string out = binary_utils::format_binary_output(png_data, "base64", "");
    std::cout << "MIME output: " << out << std::endl;
    assert(out.find("data:image/png;base64,") == 0);
    std::cout << "test_format_binary_output_mime_detection passed!" << std::endl;
}

void test_ascii85_decode() {
    // ASCII85 encoding of "Hello World"
    std::string encoded = "<~87cURD]i,\"Ebo7~>";
    std::vector<uint8_t> input(encoded.begin(), encoded.end());
    std::vector<uint8_t> decoded = binary_utils::decompress_data(input, "ascii85");
    std::string result(decoded.begin(), decoded.end());
    std::cout << "ASCII85 decoded: " << result << std::endl;
    assert(result == "Hello World");

    // Auto-detect format from "<~"
    std::vector<uint8_t> decoded_auto = binary_utils::decompress_data(input, "auto");
    std::string result_auto(decoded_auto.begin(), decoded_auto.end());
    assert(result_auto == "Hello World");

    // 'z' sequence (four zero bytes)
    std::string encoded_z = "<~z~>";
    std::vector<uint8_t> input_z(encoded_z.begin(), encoded_z.end());
    std::vector<uint8_t> decoded_z = binary_utils::decompress_data(input_z, "ascii85");
    assert(decoded_z.size() == 4);
    assert(decoded_z[0] == 0 && decoded_z[1] == 0 && decoded_z[2] == 0 && decoded_z[3] == 0);

    std::cout << "test_ascii85_decode passed!" << std::endl;
}

void test_run_length_decode() {
    // 3 reps of 'A' (254), 2 literal bytes 'B', 'C' (1), EOD (128)
    std::vector<uint8_t> input = {254, 'A', 1, 'B', 'C', 128};
    std::vector<uint8_t> decoded = binary_utils::decompress_data(input, "pdfrunlength");
    std::string result(decoded.begin(), decoded.end());
    assert(result == "AAABC");

    // Test alias "runlength"
    std::vector<uint8_t> decoded_alias = binary_utils::decompress_data(input, "runlength");
    std::string result_alias(decoded_alias.begin(), decoded_alias.end());
    assert(result_alias == "AAABC");

    std::cout << "test_run_length_decode passed!" << std::endl;
}

void test_lzw_decode() {
    auto pack_lzw = [](const std::vector<uint32_t>& codes) {
        std::vector<uint8_t> output;
        uint32_t current_byte = 0;
        size_t bits_in_byte = 0;
        for (uint32_t code : codes) {
            for (int i = 8; i >= 0; --i) {
                uint32_t bit = (code >> i) & 1;
                current_byte = (current_byte << 1) | bit;
                bits_in_byte++;
                if (bits_in_byte == 8) {
                    output.push_back(static_cast<uint8_t>(current_byte));
                    current_byte = 0;
                    bits_in_byte = 0;
                }
            }
        }
        if (bits_in_byte > 0) {
            current_byte <<= (8 - bits_in_byte);
            output.push_back(static_cast<uint8_t>(current_byte));
        }
        return output;
    };

    // LZW sequence for "AAAAA": 256 (clear), 65 ('A'), 258 ("AA"), 258 ("AA"), 257 (EOD)
    std::vector<uint8_t> input = pack_lzw({256, 65, 258, 258, 257});
    std::vector<uint8_t> decoded = binary_utils::decompress_data(input, "pdflzw");
    std::string result(decoded.begin(), decoded.end());
    assert(result == "AAAAA");

    // Test alias "lzw"
    std::vector<uint8_t> decoded_alias = binary_utils::decompress_data(input, "lzw");
    std::string result_alias(decoded_alias.begin(), decoded_alias.end());
    assert(result_alias == "AAAAA");

    std::cout << "test_lzw_decode passed!" << std::endl;
}

void test_format_binary_output_images_vfs() {
    std::vector<uint8_t> png_data = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d,
        'I', 'H', 'D', 'R',
        0x00, 0x00, 0x00, 0x0a,
        0x00, 0x00, 0x00, 0x05,
        0x08, 0x02, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    
    std::string out = binary_utils::format_binary_output(png_data, "text", "images://by-name/decompressed-test.png");
    std::cout << "Images VFS output: " << out << std::endl;
    assert(out.find("Successfully decompressed and ingested image into VFS database. URI: images://by-sha256/") == 0);
    
    std::string resolved = images::image_manager::get_instance().resolve_uri("images://by-name/decompressed-test.png");
    assert(!resolved.empty());
    assert(std::filesystem::exists(resolved));
    
    images::image_manager::get_instance().delete_image("images://by-name/decompressed-test.png");
    std::cout << "test_format_binary_output_images_vfs passed!" << std::endl;
}

int main() {
    test_watchdog::setup_watchdog(30);
    project_manager::get_instance().initialize();
    images::image_manager::get_instance().initialize();

    test_compress_decompress_all();
    test_resolve_input_data_base64();
    test_resolve_input_data_hex();
    test_format_binary_output_base64_fallback();
    test_format_binary_output_mime_detection();
    test_ascii85_decode();
    test_run_length_decode();
    test_lzw_decode();
    test_format_binary_output_images_vfs();
    std::cout << "All binary plugin unit tests passed!" << std::endl;
    return 0;
}
