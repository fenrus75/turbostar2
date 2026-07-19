#pragma once
#include "llm_tool.h"
#include "interactions/action.h"

namespace agentlib {

/*

# subclasses of llm_tool_action

| subclass     | filename                                             |
| ------------ | ---------------------------------------------------- | 
| fs_glob_tool | src/tools/fs_glob/fs_glob.h                          |
| git_blame_tool | src/tools/git_blame/git_blame.h                      |
| fs_man_tool   | src/tools/fs_man/fs_man.h                            |
| security_review_with_agent_tool | src/plugins/securityagent/security_review_with_agent/security_review_with_agent.h |
| security_verify_html_tool | src/plugins/securityagent/security_verify_html/security_verify_html.h |
| hexdump_tool | src/plugins/hexedit/hexdump_tool.h |
| hexwrite_tool | src/plugins/hexedit/hexwrite_tool.h |
| image_resize_tool | src/plugins/image_basic/image_resize_tool.h |
| image_crop_tool | src/plugins/image_basic/image_crop_tool.h |
| image_rotate_tool | src/plugins/image_basic/image_rotate_tool.h |
| image_mirror_tool | src/plugins/image_basic/image_mirror_tool.h |
| image_grayscale_tool | src/plugins/image_basic/image_grayscale_tool.h |
| image_threshold_tool | src/plugins/image_basic/image_threshold_tool.h |
| image_import_tool | src/tools/image_import/image_import.h |
| image_export_tool | src/tools/image_export/image_export.h |
| fs_purge_tmp_tool | src/tools/fs_purge_tmp/fs_purge_tmp.h |
*/
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