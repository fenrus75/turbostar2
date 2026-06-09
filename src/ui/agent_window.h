#pragma once
#include <memory>
#include "agentlib/ai_agent.h"
#include "ui/components/ui_multiline_edit.h"
#include "ui/components/ui_listbox.h"
#include "ui/window.h"

class agent_window : public window
{
      public:
	agent_window(int id, int x, int y, int width, int height, std::shared_ptr<agentlib::ai_model> model, event_queue &global_queue,
		     agentlib::document_provider *doc_provider, bool fresh_agent = false);
	agent_window(int id, int x, int y, int width, int height, std::shared_ptr<agentlib::ai_agent> existing_agent);
	~agent_window() override;

	// Intercepts events from the window's local queue
	bool process_events() override;
	void set_cursor_position() const override;

	// Called when the main loop dispatches an agent event
	void on_agent_update();

	// Override to draw the input box at the bottom
	void draw_content(bool cursor_only = false) const override;
	void draw_border() const override;
	int get_history_viewport_height() const;

	void set_sidebar_expanded(bool expanded) { sidebar_expanded_ = expanded; invalidate(); }
	bool is_sidebar_expanded() const { return sidebar_expanded_; }

	std::shared_ptr<agentlib::ai_agent> get_agent() const
	{
		return agent_;
	}

	std::string get_mouse_selected_text() const override;

	int get_scroll_offset() const { return scroll_offset_; }

      protected:
	bool update_viewport() const override;

      private:
	enum class sidebar_focus { input, todos, subagents };

	mutable int scroll_offset_{0};
	mutable int last_scroll_offset_{-1};
	mutable int max_scroll_offset_{0};
	mutable bool sidebar_expanded_{true};
	mutable sidebar_focus sidebar_focus_{sidebar_focus::input};

	std::unique_ptr<ui_multiline_edit> input_box_;
	std::shared_ptr<agentlib::ai_agent> agent_;
	mutable std::vector<agentlib::interaction_line> visible_lines_;

	std::unique_ptr<ui_listbox> todos_list_;
	std::unique_ptr<ui_listbox> subagents_list_;
};
