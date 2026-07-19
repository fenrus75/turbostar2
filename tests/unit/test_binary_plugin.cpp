#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "plugins/binary/binary_utils.h"

void test_compress_decompress_zlib() {
    std::string original = "Hello World! This is a test string to be compressed and decompressed.";
    std::vector<uint8_t> input(original.begin(), original.end());

    std::vector<uint8_t> compressed = binary_utils::compress_data(input, "zlib");
    assert(!compressed.empty());
    assert(compressed != input);

    std::vector<uint8_t> decompressed = binary_utils::decompress_data(compressed, "zlib");
    std::string result(decompressed.begin(), decompressed.end());
    assert(result == original);
    
    std::cout << "test_compress_decompress_zlib passed!" << std::endl;
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
    test_compress_decompress_zlib();
    test_resolve_input_data_base64();
    test_resolve_input_data_hex();
    std::cout << "All binary plugin unit tests passed!" << std::endl;
    return 0;
}
