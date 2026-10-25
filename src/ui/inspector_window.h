#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "call_synthesizer.h"
#include "ui/window.h"

enum class inspector_state {
	idle,
	resolving,
	active,
	blanked
};

struct markdown_token {
	std::string text;
	int color_pair{17};
	int attr{0};
};

/**
 * @brief Companion window docked at the bottom of the editor that displays live
 * ambient information about the symbol/call under the cursor from pluggable providers.
 */
class inspector_window : public window
{
      public:
	inspector_window(int id, int x, int y, int width, int height);
	~inspector_window() override = default;

	void draw_content(bool cursor_only = false) const override;
	void draw_border() const override;

	void set_markdown(std::string_view markdown, int score = 0, std::string_view provider_name = "",
			  std::string_view title_addon = "");
	void set_idle();
	void set_resolving(const std::string &token);
	void set_active_info(const std::string &token, const std::string &signature,
			     const std::vector<turbostar::call_suggestion> &proposals,
			     const std::string &location);
	void set_blanked();

	inspector_state get_state() const noexcept { return state_; }
	const std::string &get_markdown() const noexcept { return markdown_; }
	int get_score() const noexcept { return score_; }
	const std::string &get_provider_name() const noexcept { return provider_name_; }
	const std::string &get_title_addon() const noexcept { return title_addon_; }
	int get_content_height() const noexcept { return std::max(0, height_ - 2); }

	const std::string &get_token() const noexcept { return token_; }
	const std::string &get_signature() const noexcept { return signature_; }
	const std::vector<turbostar::call_suggestion> &get_proposals() const noexcept { return proposals_; }
	const std::string &get_location() const noexcept { return location_; }

	static std::vector<markdown_token> parse_markdown_line(std::string_view line);

      private:
	inspector_state state_{inspector_state::idle};
	std::string markdown_;
	int score_{0};
	std::string provider_name_;
	std::string title_addon_;

	std::string token_;
	std::string signature_;
	std::vector<turbostar::call_suggestion> proposals_;
	std::string location_;
};
