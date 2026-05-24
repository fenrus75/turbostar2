#pragma once
#include "llm_tool.h"
#include "interactions/action.h"

namespace agentlib {

class llm_tool_action : public llm_tool {
public:
    explicit llm_tool_action(std::string action_text) {
        interaction_ = std::make_shared<interaction_action>(std::move(action_text));
    }

    std::shared_ptr<agent_interaction> get_interaction() const override {
        return interaction_;
    }

protected:
    std::shared_ptr<interaction_action> interaction_;

    void set_success(tool_context& ctx, const std::string& summary = "") {
        if (interaction_) {
            interaction_->set_status(interaction_action::status::success, summary);
            if (ctx.trigger_ui_update) ctx.trigger_ui_update();
        }
    }

    void set_failure(tool_context& ctx, const std::string& error_msg = "") {
        if (interaction_) {
            interaction_->set_status(interaction_action::status::failure, error_msg);
            if (ctx.trigger_ui_update) ctx.trigger_ui_update();
        }
    }
};

} // namespace agentlib