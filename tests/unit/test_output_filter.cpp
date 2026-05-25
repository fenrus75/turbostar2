#include <cassert>
#include <iostream>
#include "src/tools/output_filter.h"

int main() {
    // 1. Test Compile Filter
    std::string command = "meson compile -C build";
    std::string raw_output = "[1/3] Compiling foo.cpp\n[2/3] Compiling bar.cpp\nWarning: unused variable 'x'\n[3/3] Linking target\n";

    std::vector<std::shared_ptr<tools::output_filter>> filters;
    filters.push_back(std::make_shared<tools::meson_compile_filter>());

    int removed = 0;
    std::string filtered_output = tools::apply_output_filters(command, raw_output, filters, &removed);

    std::string expected_output = "[2/3] Compiling bar.cpp\nWarning: unused variable 'x'\n[3/3] Linking target\n";
    
    if (filtered_output != expected_output || removed != 1) {
        std::cerr << "Compile filter test failed!\nExpected:\n" << expected_output << "\nGot:\n" << filtered_output << std::endl;
        return 1;
    }

    // 2. Test Test Filter
    std::string test_command = "MESON_TESTTHREADS=2 meson test -C build";
    std::string raw_test_output = " 1/66 unit_event_logger                OK              0.01s\n"
                                  " 2/66 unit_history_manager             OK              0.01s\n"
                                  " 3/66 unit_tool_infrastructure         FAIL            0.02s\n"
                                  " 4/66 unit_command_runner              OK              0.07s\n"
                                  " 5/66 unit_file_security               OK              0.00s\n";

    std::vector<std::shared_ptr<tools::output_filter>> test_filters;
    test_filters.push_back(std::make_shared<tools::meson_test_filter>());

    int test_removed = 0;
    std::string filtered_test_output = tools::apply_output_filters(test_command, raw_test_output, test_filters, &test_removed);

    std::string expected_test_output = " 2/66 unit_history_manager             OK              0.01s\n"
                                       " 3/66 unit_tool_infrastructure         FAIL            0.02s\n"
                                       " 5/66 unit_file_security               OK              0.00s\n";

    if (filtered_test_output != expected_test_output || test_removed != 2) {
        std::cerr << "Test filter test failed!\nExpected:\n" << expected_test_output << "\nGot:\n" << filtered_test_output << std::endl;
        return 1;
    }

    std::cout << "Output filter tests passed!" << std::endl;
    return 0;
}