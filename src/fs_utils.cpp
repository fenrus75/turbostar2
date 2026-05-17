#include "fs_utils.h"
#include "event_logger.h"

namespace fs_utils {
	std::filesystem::path safe_absolute(const std::filesystem::path& p) {
		if (p.empty()) {
			return p;
		}
		try {
			return std::filesystem::absolute(p).lexically_normal();
		} catch (const std::filesystem::filesystem_error& e) {
			event_logger::get_instance().log("Filesystem error resolving absolute path for '" + p.string() + "': " + e.what());
			return p.lexically_normal();
		} catch (...) {
			event_logger::get_instance().log("Unknown error resolving absolute path for '" + p.string() + "'");
			return p.lexically_normal();
		}
	}
}