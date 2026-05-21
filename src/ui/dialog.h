#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include "ui/ui_element.h"

enum class dialog_result {
        pending,
        confirmed,
        cancelled
};

class dialog : public ui_container{
      public:
	dialog(const std::string &title, int width, int height);
	virtual ~dialog() = default;

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;

	virtual void draw() const;
	virtual dialog_result handle_key(int key);
	virtual bool tick() { return false; }
	virtual std::optional<dialog_result> handle_mouse(int x, int y);
	virtual std::string get_result() const { return result_string_; }

	void set_result(const std::string& res) { result_string_ = res; }
	void set_action(dialog_result action) { action_ = action; }
	dialog_result get_action() const { return action_; }

      protected:
	std::string title_;
	std::string result_string_;
	dialog_result action_{dialog_result::pending};
};

