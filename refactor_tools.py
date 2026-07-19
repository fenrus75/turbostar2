#!/usr/bin/env python3
import os
import re

tools_info = [
    {"name": "image_resize", "dir": "src/plugins/image_basic", "src_field": "original_uri", "out_field": "new_uri"},
    {"name": "image_crop", "dir": "src/plugins/image_basic", "src_field": "name", "out_field": "new_uri"},
    {"name": "image_rotate", "dir": "src/plugins/image_basic", "src_field": "name", "out_field": "new_uri"},
    {"name": "image_mirror", "dir": "src/plugins/image_basic", "src_field": "name", "out_field": "new_uri"},
    {"name": "image_grayscale", "dir": "src/plugins/image_basic", "src_field": "name", "out_field": "new_uri"},
    {"name": "image_threshold", "dir": "src/plugins/image_basic", "src_field": "name", "out_field": "new_uri"},
    {"name": "image_import", "dir": "src/tools/image_import", "src_field": "output", "out_field": "new_uri", "no_src_thumb": True},
    {"name": "image_export", "dir": "src/tools/image_export", "src_field": "name", "out_field": None}
]

for info in tools_info:
    name = info["name"]
    d = info["dir"]
    src_field = info["src_field"]
    out_field = info["out_field"]
    no_src_thumb = info.get("no_src_thumb", False)
    
    h_path = f"{d}/{name}_tool.h"
    if not os.path.exists(h_path):
        h_path = f"{d}/{name}.h"
        
    cpp_path = h_path.replace(".h", ".cpp")
    if not os.path.exists(cpp_path):
        cpp_path = h_path.replace(".h", "_entry.cpp")
    
    # --- Update Header ---
    with open(h_path, "r") as f:
        h_content = f.read()
    
    if "interaction_image_tool.h" not in h_content:
        h_content = re.sub(r'(#include "agentlib/llm_tool_action.h")', r'\1\n#include "agentlib/interactions/image_tool.h"', h_content)
        
        interaction_decl = f"""
\tstd::shared_ptr<agentlib::agent_interaction> get_interaction() const override {{ return interaction_; }}
\t
      private:
\tstd::shared_ptr<agentlib::interaction_image_tool> interaction_;"""
        
        h_content = re.sub(r'(\s+private:)', interaction_decl, h_content)
        
        with open(h_path, "w") as f:
            f.write(h_content)
            
    # --- Update CPP ---
    with open(cpp_path, "r") as f:
        cpp_content = f.read()
        
    # Constructor
    class_name = f"{name}_tool"
    cons_pattern = rf"({class_name}::{class_name}\([^)]+\)\s*:.*?)\s*{{(.*?)\s*}}"
    
    def repl_cons(m):
        init_list = m.group(1)
        body = m.group(2)
        if "interaction_" not in body:
            if no_src_thumb:
                body += f'\n\tinteraction_ = std::make_shared<agentlib::interaction_image_tool>("{name}", "{name}()", "");'
            else:
                body += f'\n\tinteraction_ = std::make_shared<agentlib::interaction_image_tool>("{name}", "{name}(" + args_.{src_field} + ")", args_.{src_field});'
        return f"{init_list}\n{{\n{body}\n}}"
        
    cpp_content = re.sub(cons_pattern, repl_cons, cpp_content, flags=re.DOTALL)
    
    # Execute method
    # We need to replace all `return "string";` with `std::string res = "string"; interaction_->set_result(res); return res;`
    # Also if out_field is used, we need to call set_output_image.
    
    # It's tricky to regex replace the returns, so let's do something simpler:
    # We will replace all `return "...";` that don't already have interaction_->set_result.
    
    def repl_return(m):
        ret_val = m.group(1)
        # Avoid double replacing
        if "interaction_->set_result" in ret_val:
            return m.group(0)
            
        # Check if we should inject set_output_image
        set_out = ""
        if out_field and f"New URI: \" + {out_field}" in ret_val or f"New URI: \" + new_uri" in ret_val:
            set_out = f"interaction_->set_output_image(new_uri);\n\t\t"
        
        return f"std::string _result_msg = {ret_val};\n\t\t{set_out}interaction_->set_result(_result_msg);\n\t\treturn _result_msg;"
        
    cpp_content = re.sub(r'return\s+([^;]+);', repl_return, cpp_content)
    
    with open(cpp_path, "w") as f:
        f.write(cpp_content)

print("Refactor complete.")
