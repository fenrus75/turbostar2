#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief High-performance RAII wrapper around PCRE2 (libpcre2-8).
 *
 * Provides thread-safe, zero-allocation matching over std::string_view, JIT compilation,
 * configurable safety match limits to prevent catastrophic ReDoS, and seamless support
 * for UTF-8 and Unicode character classes.
 */
class pcre2_regex
{
      public:
	struct match_result {
		size_t byte_offset = 0;
		size_t byte_length = 0;
	};

	struct compile_options {
		bool caseless = false;
		bool multiline = false;
		bool dotall = false;
		bool jit = true;
		uint32_t match_limit = 1000000; // Safety limit against ReDoS (default 1M steps)
	};

	pcre2_regex();
	pcre2_regex(/* untrusted */ std::string_view pattern);
	pcre2_regex(/* untrusted */ std::string_view pattern, const compile_options &options);
	~pcre2_regex();

	pcre2_regex(const pcre2_regex &) = delete;
	pcre2_regex &operator=(const pcre2_regex &) = delete;
	pcre2_regex(pcre2_regex &&other) noexcept;
	pcre2_regex &operator=(pcre2_regex &&other) noexcept;

	/**
	 * @brief Returns true if compilation succeeded without errors.
	 */
	[[nodiscard]] bool is_valid() const noexcept;

	/**
	 * @brief Returns compilation error message if compilation failed.
	 */
	[[nodiscard]] const std::string &error() const noexcept;

	/**
	 * @brief Checks if the regex matches any part of subject.
	 */
	[[nodiscard]] bool partial_match(/* untrusted */ std::string_view subject) const;

	/**
	 * @brief Checks if the regex matches the full subject from start to end.
	 */
	[[nodiscard]] bool full_match(/* untrusted */ std::string_view subject) const;

	/**
	 * @brief Finds the first match in subject at or after start_byte_offset.
	 */
	[[nodiscard]] std::optional<match_result> find_next(/* untrusted */ std::string_view subject,
							    size_t start_byte_offset = 0) const;

	/**
	 * @brief Finds all non-overlapping matches in subject.
	 */
	[[nodiscard]] std::vector<match_result> find_all(/* untrusted */ std::string_view subject) const;

	/**
	 * @brief Replaces all matches in subject with replacement (supporting $1, \1 capture backreferences).
	 */
	[[nodiscard]] std::string global_replace(/* untrusted */ std::string_view subject,
						 std::string_view replacement) const;

	/**
	 * @brief Escapes regex meta-characters in text for exact literal matching.
	 */
	[[nodiscard]] static std::string quote_meta(std::string_view text);

      private:
	struct impl;
	std::unique_ptr<impl> impl_;
};
