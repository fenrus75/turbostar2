#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "plugins/binary/binary_utils.h"

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

int main() {
    test_watchdog::setup_watchdog(30);
    test_compress_decompress_all();
    test_resolve_input_data_base64();
    test_resolve_input_data_hex();
    test_format_binary_output_base64_fallback();
    test_format_binary_output_mime_detection();
    std::cout << "All binary plugin unit tests passed!" << std::endl;
    return 0;
}
