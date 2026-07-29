#!/usr/bin/env python3
import sys
import re
from pathlib import Path

HEADER_PATH = Path(__file__).parent.parent / "src" / "agentlib" / "stale_models.h"

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 scripts/add_stale_model.py <model_id>")
        sys.exit(1)

    new_model = sys.argv[1].strip()
    if not new_model:
        print("Error: empty model_id provided.")
        sys.exit(1)

    content = HEADER_PATH.read_text(encoding="utf-8")

    # Match items in array
    array_match = re.search(r"constexpr std::array<std::string_view,\s*\d+>\s*STALE_MODELS\s*=\s*\{([^}]+)\};", content)
    if not array_match:
        print("Error: Could not locate STALE_MODELS array in stale_models.h")
        sys.exit(1)

    raw_items = array_match.group(1)
    models = set(re.findall(r'"([^"]+)"', raw_items))
    models.add(new_model)
    sorted_models = sorted(models)

    formatted_items = ",\n".join(f'    "{m}"' for m in sorted_models)
    replacement = f"constexpr std::array<std::string_view, {len(sorted_models)}> STALE_MODELS = {{\n{formatted_items}\n}};"

    new_content = re.sub(r"constexpr std::array<std::string_view,\s*\d+>\s*STALE_MODELS\s*=\s*\{([^}]+)\};", replacement, content)
    HEADER_PATH.write_text(new_content, encoding="utf-8")

    print(f"Successfully added '{new_model}' to {HEADER_PATH.name} (Total stale models: {len(sorted_models)}).")

if __name__ == "__main__":
    main()
