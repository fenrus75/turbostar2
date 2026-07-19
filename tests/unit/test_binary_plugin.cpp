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

int main() {
    test_watchdog::setup_watchdog(30);
    test_compress_decompress_all();
    test_resolve_input_data_base64();
    test_resolve_input_data_hex();
    std::cout << "All binary plugin unit tests passed!" << std::endl;
    return 0;
}
