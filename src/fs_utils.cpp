#include "fs_utils.h"
#include "event_logger.h"

namespace fs_utils {
	std::filesystem::path safe_absolute(const std::filesystem::path& p) {
		if (p.empty()) {
			return p;
		}
		try {
			return std::filesystem::absolute(p);
		} catch (const std::filesystem::filesystem_error& e) {
			event_logger::get_instance().log("Filesystem error resolving absolute path for '" + p.string() + "': " + e.what());
			return p;
		} catch (...) {
			event_logger::get_instance().log("Unknown error resolving absolute path for '" + p.string() + "'");
			return p;
		}
	}
}