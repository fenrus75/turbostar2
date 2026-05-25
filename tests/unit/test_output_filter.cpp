#include <cassert>
#include <iostream>
#include "src/tools/output_filter.h"

int main() {
    std::string command = "meson compile -C build";
    std::string raw_output = "[1/3] Compiling foo.cpp\n[2/3] Compiling bar.cpp\nWarning: unused variable 'x'\n[3/3] Linking target\n";

    std::vector<std::shared_ptr<tools::output_filter>> filters;
    filters.push_back(std::make_shared<tools::meson_compile_filter>());

    std::string filtered_output = tools::apply_output_filters(command, raw_output, filters);

    std::string expected_output = "[2/3] Compiling bar.cpp\nWarning: unused variable 'x'\n[3/3] Linking target\n";
    
    if (filtered_output == expected_output) {
        std::cout << "Output filter test passed!" << std::endl;
        return 0;
    } else {
        std::cerr << "Output filter test failed!\nExpected:\n" << expected_output << "\nGot:\n" << filtered_output << std::endl;
        return 1;
    }
}