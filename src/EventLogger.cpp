#include "EventLogger.hpp"
#include <algorithm>

EventLogger& EventLogger::getInstance() {
    static EventLogger instance;
    return instance;
}

void EventLogger::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    events.push_back(message);
}

void EventLogger::writeToFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream out(filename);
    if (out.is_open()) {
        for (const auto& event : events) {
            out << event << "\n";
        }
    }
}

std::optional<std::string> EventLogger::getLatestMatchingMessage(const std::string& substring) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        if (substring.empty() || it->find(substring) != std::string::npos) {
            return *it;
        }
    }
    return std::nullopt;
}
