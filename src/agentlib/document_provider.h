#pragma once
#include <string>
#include <memory>
#include <vector>

namespace agentlib {

struct start_app_result {
	int app_run_id = -1;
	int gdb_run_id = -1;
};

struct run_screenshot_data {
	std::vector<std::string> grid;
	int cursor_x = 0;
	int cursor_y = 0;
	bool cursor_visible = false;
};

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

    // Dispatches a batch of edits to the main UI thread to be applied to the live document.
    virtual bool apply_live_edits(const std::string& safe_path, const std::string& edits_json_payload) = 0;

    // Forces the editor to save all open, modified documents to disk synchronously.
    // Useful before invoking external tools (compilers, git) that expect the disk to be up-to-date.
    virtual void save_all_documents() = 0;

    // Unified app execution and debugging agent APIs
    virtual start_app_result start_app(const std::string& /*args*/, bool /*use_debugger*/) { return {-1, -1}; }
    virtual bool write_to_run(int /*run_id*/, const std::string& /*data*/) { return false; }
    virtual run_screenshot_data get_run_screenshot(int /*run_id*/) { return {}; }
    virtual bool terminate_run(int /*run_id*/) { return false; }
};

} // namespace agentlib
