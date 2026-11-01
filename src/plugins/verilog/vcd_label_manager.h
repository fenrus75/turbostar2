#pragma once

#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace verilog
{

class vcd_label_manager
{
      public:
	static vcd_label_manager &get_instance();

	// Normalizes a hex/binary/decimal string to lowercase 0x... format
	static std::string normalize_hex_key(std::string_view val);

	// Get all registered labels for a VCD path (loads sidecar file if not loaded)
	std::map<std::string, std::map<std::string, std::string>> get_labels_for_file(std::string_view safe_path);

	// Find label for a given signal and value
	// hex_val: e.g. "0x06"
	// raw_bin_val: raw binary string e.g. "00000110" (for wildcard binary matching like "00????10")
	std::string find_label(
		std::string_view safe_path,
		std::string_view signal_name,
		std::string_view hex_val,
		std::string_view raw_bin_val = ""
	);

	// Register or clear labels for a signal
	// If clear is true, removes labels for the signal (or all signals if signal_name is empty)
	bool set_labels(
		std::string_view safe_path,
		std::string_view signal_name,
		const std::map<std::string, std::string> &val_to_label,
		bool clear,
		bool &out_file_saved,
		std::string &out_msg
	);

	// Clear all in-memory labels (useful on plugin unload or test teardown)
	void clear_all();

      private:
	vcd_label_manager() = default;
	~vcd_label_manager() = default;

	/*
	 * mutex_ protects the in-memory cache of file labels:
	 * path_labels_ map (safe_path -> (signal -> (hex_key -> label_string)))
	 *
	 * Locking Rules:
	 * - Held briefly during get, find, and update operations.
	 */
	mutable std::mutex mutex_;
	std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> path_labels_;
	std::map<std::string, bool> file_loaded_;

	void load_sidecar_locked(std::string_view safe_path);
	bool save_sidecar_locked(std::string_view safe_path, std::string &out_error);
};

} // namespace verilog
