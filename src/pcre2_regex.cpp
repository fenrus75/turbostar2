#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "pcre2_regex.h"
#include <algorithm>
#include <format>

struct pcre2_regex::impl
{
	pcre2_code *code = nullptr;
	std::string error_message;
	uint32_t match_limit = 0;

	~impl()
	{
		if (code) {
			pcre2_code_free(code);
			code = nullptr;
		}
	}
};

pcre2_regex::pcre2_regex() = default;

pcre2_regex::pcre2_regex(/* untrusted */ std::string_view pattern)
	: pcre2_regex(pattern, compile_options{})
{
}

pcre2_regex::pcre2_regex(/* untrusted */ std::string_view pattern, const compile_options &options)
	: impl_(std::make_unique<impl>())
{
	int errorcode = 0;
	PCRE2_SIZE erroroffset = 0;

	uint32_t compile_flags = PCRE2_UTF | PCRE2_UCP;
	if (options.caseless) {
		compile_flags |= PCRE2_CASELESS;
	}
	if (options.multiline) {
		compile_flags |= PCRE2_MULTILINE;
	}
	if (options.dotall) {
		compile_flags |= PCRE2_DOTALL;
	}

	impl_->code = pcre2_compile(
		reinterpret_cast<PCRE2_SPTR>(pattern.data()),
		pattern.size(),
		compile_flags,
		&errorcode,
		&erroroffset,
		nullptr
	);

	if (!impl_->code) {
		PCRE2_UCHAR buffer[256];
		pcre2_get_error_message(errorcode, buffer, sizeof(buffer));
		impl_->error_message = std::format("PCRE2 error {} at offset {}: {}", errorcode, erroroffset, reinterpret_cast<char *>(buffer));
		return;
	}

	impl_->match_limit = options.match_limit;

	if (options.jit) {
		pcre2_jit_compile(impl_->code, PCRE2_JIT_COMPLETE);
	}
}

pcre2_regex::~pcre2_regex() = default;

pcre2_regex::pcre2_regex(pcre2_regex &&other) noexcept = default;
pcre2_regex &pcre2_regex::operator=(pcre2_regex &&other) noexcept = default;

bool pcre2_regex::is_valid() const noexcept
{
	return impl_ != nullptr && impl_->code != nullptr;
}

const std::string &pcre2_regex::error() const noexcept
{
	static const std::string uninit_err = "Uninitialized regex";
	if (!impl_) {
		return uninit_err;
	}
	return impl_->error_message;
}

bool pcre2_regex::partial_match(/* untrusted */ std::string_view subject) const
{
	if (!is_valid()) {
		return false;
	}

	pcre2_match_context *mcontext = nullptr;
	if (impl_->match_limit > 0) {
		mcontext = pcre2_match_context_create(nullptr);
		pcre2_set_match_limit(mcontext, impl_->match_limit);
	}

	pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(impl_->code, nullptr);
	int rc = pcre2_match(
		impl_->code,
		reinterpret_cast<PCRE2_SPTR>(subject.data()),
		subject.size(),
		0,
		0,
		match_data,
		mcontext
	);

	pcre2_match_data_free(match_data);
	if (mcontext) {
		pcre2_match_context_free(mcontext);
	}

	return rc >= 0;
}

bool pcre2_regex::full_match(/* untrusted */ std::string_view subject) const
{
	if (!is_valid()) {
		return false;
	}

	pcre2_match_context *mcontext = nullptr;
	if (impl_->match_limit > 0) {
		mcontext = pcre2_match_context_create(nullptr);
		pcre2_set_match_limit(mcontext, impl_->match_limit);
	}

	pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(impl_->code, nullptr);
	int rc = pcre2_match(
		impl_->code,
		reinterpret_cast<PCRE2_SPTR>(subject.data()),
		subject.size(),
		0,
		PCRE2_ANCHORED | PCRE2_ENDANCHORED,
		match_data,
		mcontext
	);

	pcre2_match_data_free(match_data);
	if (mcontext) {
		pcre2_match_context_free(mcontext);
	}

	return rc >= 0;
}

std::optional<pcre2_regex::match_result> pcre2_regex::find_next(/* untrusted */ std::string_view subject,
							       size_t start_byte_offset) const
{
	if (!is_valid() || start_byte_offset > subject.size()) {
		return std::nullopt;
	}

	pcre2_match_context *mcontext = nullptr;
	if (impl_->match_limit > 0) {
		mcontext = pcre2_match_context_create(nullptr);
		pcre2_set_match_limit(mcontext, impl_->match_limit);
	}

	pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(impl_->code, nullptr);
	int rc = pcre2_match(
		impl_->code,
		reinterpret_cast<PCRE2_SPTR>(subject.data()),
		subject.size(),
		start_byte_offset,
		0,
		match_data,
		mcontext
	);

	std::optional<match_result> result;
	if (rc >= 0) {
		PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
		result = match_result{
			.byte_offset = ovector[0],
			.byte_length = ovector[1] - ovector[0]
		};
	}

	pcre2_match_data_free(match_data);
	if (mcontext) {
		pcre2_match_context_free(mcontext);
	}

	return result;
}

std::vector<pcre2_regex::match_result> pcre2_regex::find_all(/* untrusted */ std::string_view subject) const
{
	if (!is_valid()) {
		return {};
	}

	pcre2_match_context *mcontext = nullptr;
	if (impl_->match_limit > 0) {
		mcontext = pcre2_match_context_create(nullptr);
		pcre2_set_match_limit(mcontext, impl_->match_limit);
	}

	pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(impl_->code, nullptr);
	std::vector<match_result> results;

	size_t offset = 0;
	while (offset <= subject.size()) {
		int rc = pcre2_match(
			impl_->code,
			reinterpret_cast<PCRE2_SPTR>(subject.data()),
			subject.size(),
			offset,
			0,
			match_data,
			mcontext
		);

		if (rc < 0) {
			break;
		}

		PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
		results.push_back({
			.byte_offset = ovector[0],
			.byte_length = ovector[1] - ovector[0]
		});

		offset = ovector[1];
		if (ovector[0] == ovector[1]) {
			// Zero-length match: step 1 byte forward to avoid infinite loop
			offset++;
		}
	}

	pcre2_match_data_free(match_data);
	if (mcontext) {
		pcre2_match_context_free(mcontext);
	}

	return results;
}

std::string pcre2_regex::global_replace(/* untrusted */ std::string_view subject,
					std::string_view replacement) const
{
	if (!is_valid()) {
		return std::string(subject);
	}

	std::string result;
	result.resize(std::max<size_t>(subject.size() * 2, 256));
	PCRE2_SIZE out_len = result.size();

	pcre2_match_context *mcontext = nullptr;
	if (impl_->match_limit > 0) {
		mcontext = pcre2_match_context_create(nullptr);
		pcre2_set_match_limit(mcontext, impl_->match_limit);
	}

	int rc = pcre2_substitute(
		impl_->code,
		reinterpret_cast<PCRE2_SPTR>(subject.data()),
		subject.size(),
		0,
		PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED,
		nullptr,
		mcontext,
		reinterpret_cast<PCRE2_SPTR>(replacement.data()),
		replacement.size(),
		reinterpret_cast<PCRE2_UCHAR *>(result.data()),
		&out_len
	);

	if (rc == PCRE2_ERROR_NOMEMORY) {
		result.resize(out_len);
		rc = pcre2_substitute(
			impl_->code,
			reinterpret_cast<PCRE2_SPTR>(subject.data()),
			subject.size(),
			0,
			PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED,
			nullptr,
			mcontext,
			reinterpret_cast<PCRE2_SPTR>(replacement.data()),
			replacement.size(),
			reinterpret_cast<PCRE2_UCHAR *>(result.data()),
			&out_len
		);
	}

	if (mcontext) {
		pcre2_match_context_free(mcontext);
	}

	if (rc >= 0) {
		result.resize(out_len);
		return result;
	}

	return std::string(subject);
}

std::string pcre2_regex::quote_meta(std::string_view text)
{
	std::string result;
	result.reserve(text.size() * 2);
	for (char c : text) {
		switch (c) {
		case '\\':
		case '^':
		case '$':
		case '.':
		case '[':
		case ']':
		case '|':
		case '(':
		case ')':
		case '?':
		case '*':
		case '+':
		case '{':
		case '}':
		case '-':
		case '/':
			result.push_back('\\');
			result.push_back(c);
			break;
		default:
			result.push_back(c);
			break;
		}
	}
	return result;
}
