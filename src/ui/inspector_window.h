#pragma once

#include <string>
#include <vector>
#include "call_synthesizer.h"
#include "ui/window.h"

enum class inspector_state {
	idle,
	resolving,
	active,
	blanked
};

/**
 * @brief Companion window docked at the bottom of the editor that displays live
 * ambient information about the symbol/call under the cursor.
 */
class inspector_window : public window
{
      public:
	inspector_window(int id, int x, int y, int width, int height);
	~inspector_window() override = default;

	void draw_content(bool cursor_only = false) const override;

	void set_idle();
	void set_resolving(const std::string &token);
	void set_active_info(const std::string &token, const std::string &signature,
			     const std::vector<turbostar::call_suggestion> &proposals,
			     const std::string &location);
	void set_blanked();

	inspector_state get_state() const noexcept { return state_; }
	const std::string &get_token() const noexcept { return token_; }
	const std::string &get_signature() const noexcept { return signature_; }
	const std::vector<turbostar::call_suggestion> &get_proposals() const noexcept { return proposals_; }
	const std::string &get_location() const noexcept { return location_; }

      private:
	inspector_state state_{inspector_state::idle};
	std::string token_;
	std::string signature_;
	std::vector<turbostar::call_suggestion> proposals_;
	std::string location_;
};

