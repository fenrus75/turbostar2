#!/usr/bin/env python3
import json
import os
import re
import subprocess
import sys

try:
    import jsonschema
except ImportError:
    jsonschema = None


def main():
    build_dir = os.environ.get("MESON_BUILD_ROOT", "build")
    turbostar_bin = os.path.join(build_dir, "turbostar")

    if not os.path.exists(turbostar_bin):
        print(f"Error: {turbostar_bin} not found.")
        sys.exit(1)

    print(f"Starting {turbostar_bin} --mcp to test tools/list compliance...")
    proc = subprocess.Popen(
        [turbostar_bin, "--mcp"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    req = {"jsonrpc": "2.0", "id": 100, "method": "tools/list"}
    try:
        stdout_data, stderr_data = proc.communicate(input=json.dumps(req) + "\n", timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()
        print("Error: MCP server timed out on tools/list request.")
        sys.exit(1)

    if proc.returncode != 0 and proc.returncode is not None:
        print(f"Error: MCP server exited with code {proc.returncode}")
        print("Stderr:", stderr_data)
        sys.exit(1)

    lines = [line.strip() for line in stdout_data.splitlines() if line.strip()]
    if not lines:
        print("Error: Empty response from MCP server.")
        sys.exit(1)

    try:
        resp = json.loads(lines[0])
    except Exception as e:
        print(f"Error parsing MCP response JSON: {e}")
        print("Raw response:", stdout_data[:500])
        sys.exit(1)

    assert resp.get("jsonrpc") == "2.0", f"Invalid jsonrpc: {resp.get('jsonrpc')}"
    assert resp.get("id") == 100, f"Mismatched response ID: {resp.get('id')}"
    assert "result" in resp, f"Response missing 'result': {resp}"
    assert "tools" in resp["result"], f"Response missing 'tools': {resp['result']}"

    tools = resp["result"]["tools"]
    print(f"Received {len(tools)} tools from MCP server. Performing compliance checks...")

    errors = []
    tool_names = set()

    for tool in tools:
        name = tool.get("name")
        if not name or not isinstance(name, str):
            errors.append(f"Tool missing valid string name: {tool}")
            continue

        tool_names.add(name)

        if not re.match(r"^[a-zA-Z0-9_-]{1,64}$", name):
            errors.append(f"Tool '{name}': name violates MCP naming convention (^[a-zA-Z0-9_-]{{1,64}}$)")

        desc = tool.get("description")
        if desc is not None and not isinstance(desc, str):
            errors.append(f"Tool '{name}': description must be a string if provided")

        schema = tool.get("inputSchema")
        if not isinstance(schema, dict):
            errors.append(f"Tool '{name}': inputSchema must be a JSON object, got {type(schema)}")
            continue

        if schema.get("type") != "object":
            errors.append(f"Tool '{name}': inputSchema.type must be 'object', got {schema.get('type')}")

        # Check properties
        props = schema.get("properties")
        if props is not None:
            if not isinstance(props, dict):
                errors.append(f"Tool '{name}': inputSchema.properties must be a dict, got {type(props)}")
            else:
                for prop_name, prop_val in props.items():
                    if not isinstance(prop_val, dict):
                        errors.append(
                            f"Tool '{name}', property '{prop_name}': must be a JSON object schema, got {type(prop_val)} ({repr(prop_val)[:60]})"
                        )
                    else:
                        p_type = prop_val.get("type")
                        if p_type == "array":
                            items = prop_val.get("items")
                            if items is not None and not isinstance(items, (dict, list)):
                                errors.append(f"Tool '{name}', property '{prop_name}': array items must be a schema object")
                        if "enum" in prop_val and not isinstance(prop_val["enum"], list):
                            errors.append(f"Tool '{name}', property '{prop_name}': enum must be a list")

        # Check required fields
        req_fields = schema.get("required")
        if req_fields is not None:
            if not isinstance(req_fields, list):
                errors.append(f"Tool '{name}': inputSchema.required must be a list, got {type(req_fields)}")
            else:
                for r in req_fields:
                    if not isinstance(r, str):
                        errors.append(f"Tool '{name}': required item '{r}' must be a string")

        # Official JSON Schema meta-schema validation if library is available
        if jsonschema is not None:
            try:
                jsonschema.Draft7Validator.check_schema(schema)
            except Exception as e:
                errors.append(f"Tool '{name}': failed Draft 7 JSON Schema validation: {str(e).splitlines()[0]}")
            try:
                jsonschema.Draft202012Validator.check_schema(schema)
            except Exception as e:
                errors.append(f"Tool '{name}': failed Draft 2020-12 JSON Schema validation: {str(e).splitlines()[0]}")

    # Check key tools presence
    for required_tool in ["fs_read_lines", "fs_batch_read", "fs_replace_content"]:
        if required_tool not in tool_names:
            errors.append(f"Expected core tool '{required_tool}' was not returned by MCP tools/list")

    if errors:
        print(f"\nFAILED: Found {len(errors)} compliance violation(s):")
        for err in errors:
            print(" -", err)
        sys.exit(1)

    print(f"SUCCESS: All {len(tools)} MCP tools passed strict JSON Schema & MCP standards compliance checks!")
    sys.exit(0)


if __name__ == "__main__":
    main()
