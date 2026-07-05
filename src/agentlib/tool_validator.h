#pragma once
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include "tool_context.h"
#include "llm_tool.h"
#include "agent_properties.h"

namespace agentlib {

/*

# subclasses of tool_validator

| subclass                     | filename                                                    |
| ---------------------------- | ----------------------------------------------------------- | 
| fs_man_validator             | src/tools/fs_man/fs_man.h                                   |
| fs_replace_content_validator | src/tools/fs_replace_content/fs_replace_content.h           |
| fs_list_dir_validator        | src/tools/fs_list_dir/fs_list_dir.h                          |
| confirm_code_review_item_validator | src/tools/confirm_code_review_item/confirm_code_review_item.h |
| resolve_code_review_item_validator | src/tools/resolve_code_review_item/resolve_code_review_item.h |
| list_code_review_items_validator   | src/tools/list_code_review_items/list_code_review_items.h   |
| get_code_review_item_validator     | src/tools/get_code_review_item/get_code_review_item.h       |
| security_review_with_agent_validator | src/plugins/securityagent/security_review_with_agent/security_review_with_agent.h |
| git_blame_validator          | src/tools/git_blame/git_blame.h                              |
| security_verify_html_validator | src/plugins/securityagent/security_verify_html/security_verify_html.h |
| hexdump_validator | src/plugins/hexedit/hexdump_validator.h |
| hexwrite_validator | src/plugins/hexedit/hexwrite_validator.h |
| apply_text_filter_validator | src/tools/apply_text_filter/apply_text_filter.h |
| image_resize_validator | src/plugins/image_basic/image_resize_validator.h |
| image_crop_validator | src/plugins/image_basic/image_crop_validator.h |
| image_rotate_validator | src/plugins/image_basic/image_rotate_validator.h |
| image_mirror_validator | src/plugins/image_basic/image_mirror_validator.h |
| image_grayscale_validator | src/plugins/image_basic/image_grayscale_validator.h |
| image_threshold_validator | src/plugins/image_basic/image_threshold_validator.h |
*/
class tool_validator {
public:
    virtual ~tool_validator() = default;

    // Schema definition for the LLM payload
    virtual std::string get_name() const = 0;
    virtual std::string get_description() const = 0;
    virtual nlohmann::json get_parameters_schema() const = 0;

    // Indicates if the tool is "pure" (has no side effects).
    // Pure tools (e.g., read, list, status) can be safely executed repeatedly.
    virtual bool is_pure() const { return false; }

    // Returns the tool family name (default is "base")
    virtual std::string get_family() const { return "base"; }

    // Indicates if the tool's execution should be hidden from the UI by default.
    virtual bool is_silent_by_default() const { return is_pure(); }

    // Indicates if the tool can be executed while the agent is in plan mode.
    // By default, pure tools are allowed.
    virtual bool is_allowed_in_plan_mode(const nlohmann::json& /*args*/, const tool_context& /*ctx*/) const {
        return is_pure();
    }

    // Indicates if the tool can be executed by the given agent properties.
    // By default, summarizers are allowed no tools, while other roles can run any tool.
    virtual bool is_allowed_for_agent(const agent_properties &properties) const;

    // Non-Virtual Interface (NVI): Enforces state and execution order.
    // Parses and validates args before the tool is allowed to be instantiated.
    bool validate_args(const nlohmann::json& args, const tool_context& ctx, std::string& out_error) {
        is_validated_ = false;
        
        // Centralized Automated Schema Validation
        nlohmann::json schema = get_parameters_schema();
        if (!ctx.mutation_possible) {
            if (schema.contains("properties") && schema["properties"].is_object()) {
                schema["properties"].erase("async");
                schema["properties"].erase("is_async");
            }
            if (schema.contains("required") && schema["required"].is_array()) {
                nlohmann::json new_req = nlohmann::json::array();
                for (const auto &item : schema["required"]) {
                    if (item.is_string() && (item.get<std::string>() == "async" || item.get<std::string>() == "is_async")) {
                        continue;
                    }
                    new_req.push_back(item);
                }
                schema["required"] = new_req;
            }
        }
        if (schema.contains("required") && schema["required"].is_array()) {
            for (const auto& req : schema["required"]) {
                if (!req.is_string()) continue;
                std::string key = req.get<std::string>();
                if (!args.contains(key)) {
                    out_error = "Schema Validation Failed: Missing required argument '" + key + "'";
                    return false;
                }
            }
        }

        if (schema.contains("properties") && schema["properties"].is_object()) {
            for (auto it = args.begin(); it != args.end(); ++it) {
                if (!schema["properties"].contains(it.key())) {
                    out_error = "Schema Validation Failed: Unexpected argument '" + it.key() + "'";
                    return false;
                }
                auto prop_schema = schema["properties"][it.key()];
                if (prop_schema.contains("type") && prop_schema["type"].is_string()) {
                    std::string expected_type = prop_schema["type"].get<std::string>();
                    bool type_ok = false;
                    if (expected_type == "string" && it.value().is_string()) type_ok = true;
                    else if (expected_type == "integer" && it.value().is_number_integer()) type_ok = true;
                    else if (expected_type == "boolean" && it.value().is_boolean()) type_ok = true;
                    else if (expected_type == "array" && it.value().is_array()) type_ok = true;
                    else if (expected_type == "object" && it.value().is_object()) type_ok = true;
                    else if (expected_type == "number" && it.value().is_number()) type_ok = true;

                    if (!type_ok) {
                        out_error = "Schema Validation Failed: Type mismatch for argument '" + it.key() + "'. Expected " + expected_type;
                        return false;
                    }
                }
            }
        }

        // Delegate to tool-specific validation
        if (validate_args_impl(args, ctx, out_error)) {
            is_validated_ = true;
            return true;
        }
        return false;
    }

    // Instantiates the actual tool. STRICTLY FAILS if validate_args was not successful.
    std::unique_ptr<llm_tool> create_tool(const nlohmann::json& args) const {
        if (!is_validated_) {
            return nullptr;
        }
        return create_tool_impl(args);
    }

protected:
    // Derived classes MUST implement these protected methods instead of the public ones.
    virtual bool validate_args_impl(const nlohmann::json& args, const tool_context& ctx, std::string& out_error) const = 0;
    virtual std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json& args) const = 0;

private:
    bool is_validated_ = false;
};

} // namespace agentlib
