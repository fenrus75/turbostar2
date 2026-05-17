#pragma once
#include <string>
#include <memory>

namespace agentlib {

// An abstract snapshot of a document's state.
// Implementations of this should be thread-safe copies of the editor's live state.
struct diagnostic_snapshot {
    int line;
    int column;
    std::string severity;
    std::string message;
    std::string source;
};

class document_snapshot {
public:
    virtual ~document_snapshot() = default;
    virtual size_t get_line_count() const = 0;
    
    // index is 0-based
    virtual std::string get_line_text(size_t index) const = 0;

    virtual std::vector<diagnostic_snapshot> get_diagnostics() const = 0;
};

// Interface for the tool framework to query the core editor
class document_provider {
public:
    virtual ~document_provider() = default;
    
    // Returns a list of all currently open document paths (absolute, canonicalized)
    virtual std::vector<std::string> get_open_document_paths() const = 0;

    // Returns a snapshot if the file is currently open in the editor, else nullptr.
    // The path provided here MUST be the absolute, canonicalized safe_path.
    virtual std::unique_ptr<document_snapshot> get_open_document(const std::string& safe_path) const = 0;
};

} // namespace agentlib
