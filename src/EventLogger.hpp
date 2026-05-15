#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <optional>

class EventLogger {
public:
    static EventLogger& getInstance();

    void log(const std::string& message);
    void writeToFile(const std::string& filename);
    std::optional<std::string> getLatestMatchingMessage(const std::string& substring) const;

private:
    EventLogger() = default;
    ~EventLogger() = default;
    EventLogger(const EventLogger&) = delete;
    EventLogger& operator=(const EventLogger&) = delete;

    std::vector<std::string> events;
    mutable std::mutex mutex_;
};
