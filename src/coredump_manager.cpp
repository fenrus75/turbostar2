#include "coredump_manager.h"
#include "command_runner.h"
#include "agentlib/ai_model.h" // For registry
#include "agentlib/ai_agent.h" // For sending context
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>

std::string coredump_info::to_markdown_row() const {
    std::ostringstream oss;
    oss << "| " << pid << " | " << timestamp << " | `" << executable << "` | " << signal << " |";
    return oss.str();
}

coredump_manager& coredump_manager::get_instance() {
    static coredump_manager instance;
    return instance;
}

std::string coredump_manager::refresh(const std::string& project_hash) {
    std::lock_guard<std::mutex> lock(mutex_);

    sync_command_runner runner;
    runner.set_bypass_coredump_check(true); // Prevent infinite recursion
    
    // We only care about coredumps from our specific project namespace
    std::string unit_prefix = "turbostar-project-" + project_hash;

    // Fetch the list in JSON format
    std::string list_output = runner.execute_and_get_output("coredumpctl --user list -o json");
    if (list_output.empty()) return "";

    std::string new_coredumps_report;

    try {
        // coredumpctl -o json outputs a stream of JSON objects (JSON Lines), not a single array.
        // We need to parse line by line.
        std::istringstream iss(list_output);
        std::string line;
        
        bool found_new = false;

        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            
            nlohmann::json dump_json;
            try {
                dump_json = nlohmann::json::parse(line);
            } catch (...) {
                continue; // Ignore non-json lines
            }
            
            // Check if it belongs to our namespace
            std::string unit = "";
            if (dump_json.contains("COREDUMP_USER_UNIT") && dump_json["COREDUMP_USER_UNIT"].is_string()) {
                unit = dump_json["COREDUMP_USER_UNIT"].get<std::string>();
            } else if (dump_json.contains("COREDUMP_UNIT") && dump_json["COREDUMP_UNIT"].is_string()) {
                unit = dump_json["COREDUMP_UNIT"].get<std::string>();
            }

            if (!unit.starts_with(unit_prefix)) continue;

            int pid = -1;
            if (dump_json.contains("COREDUMP_PID")) {
                 if (dump_json["COREDUMP_PID"].is_string()) {
                     pid = std::stoi(dump_json["COREDUMP_PID"].get<std::string>());
                 } else if (dump_json["COREDUMP_PID"].is_number()) {
                     pid = dump_json["COREDUMP_PID"].get<int>();
                 }
            }
            if (pid == -1 || seen_pids_.contains(pid)) continue;

            coredump_info info;
            info.pid = pid;
            info.unit = unit;

            if (dump_json.contains("COREDUMP_TIMESTAMP")) {
                 // Convert microsecond timestamp to a readable string (rudimentary)
                 info.timestamp = dump_json["COREDUMP_TIMESTAMP"].get<std::string>(); 
            }
            
            if (dump_json.contains("COREDUMP_EXE")) {
                info.executable = dump_json["COREDUMP_EXE"].get<std::string>();
            }

            if (dump_json.contains("COREDUMP_SIGNAL")) {
                info.signal = dump_json["COREDUMP_SIGNAL"].get<std::string>();
            }

            // Fetch detailed info
            std::string info_cmd = "coredumpctl --user info " + std::to_string(pid);
            info.raw_info = runner.execute_and_get_output(info_cmd);

            coredumps_.push_back(info);
            seen_pids_.insert(pid);
            
            if (!found_new) {
                new_coredumps_report = "### New Coredumps Detected\n| PID | Timestamp | Executable | Signal |\n|---|---|---|---|\n";
            }
            
            new_coredumps_report += info.to_markdown_row() + "\n";
            found_new = true;
        }

    } catch (const std::exception& e) {
        std::cerr << "Failed to parse coredumpctl output: " << e.what() << "\n";
    }
    
    return new_coredumps_report;
}

const std::vector<coredump_info>& coredump_manager::get_coredumps() const {
    return coredumps_;
}

std::string coredump_manager::get_markdown_table() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    if (coredumps_.empty()) return "No coredumps recorded.";

    std::ostringstream oss;
    oss << "| PID | Timestamp | Executable | Signal |\n";
    oss << "|---|---|---|---|\n";
    for (const auto& dump : coredumps_) {
        oss << dump.to_markdown_row() << "\n";
    }
    return oss.str();
}