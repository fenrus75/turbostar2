# Plugin Architecture and Design

Turbostar supports extending the application's functionality via dynamically loaded plugins. This document describes the dynamic loading architecture, lifecycle management, and rules for registering and unregistering tools within plugins.

---

## 1. Directory Structure and Discovery

Plugins are compiled as shared modules (`.so` files) and placed in the plugin directory specified at compile time by the `PLUGIN_DIR` macro (typically resolving to `/usr/local/lib/turbostar/plugins` or similar path in the user's home directory).

The `plugin_loader` singleton scans this directory, loads valid `.so` files using `dlopen`, queries their metadata and entry points, and manages their lifecycles.

---

## 2. Shared Module Interface

Every plugin must export a clean C-linkage interface (`extern "C"`) to prevent C++ name mangling, allowing the host application to resolve entry points via `dlsym`.

### Mandatory Metadata & Entry Points

Each plugin must implement the following four entry points:

| Symbol Name | Return Type | Description |
|---|---|---|
| `plugin_name` | `const char *` | Returns the human-readable name of the plugin (used in Help -> Plugins... dialog). |
| `plugin_description` | `const char *` | Returns a short description of the plugin's purpose and functionality. |
| `plugin_run` | `void` | Serving as the main entry point called programmatically immediately after the plugin is `dlopen`ed. |
| `plugin_unload` | `void` | Called programmatically right before the plugin is closed with `dlclose`. |

### Example Implementation (`plugin.cpp`)

```cpp
extern "C" {

const char *plugin_name(void)
{
	return "x86 Assembler & Disassembler";
}

const char *plugin_description(void)
{
	return "Provides tools for assembling and disassembling x86 machine instructions.";
}

// Programmatic tool registration declarations
void register_x86_assemble(void);
void unregister_x86_assemble(void);
void register_x86_disassemble(void);
void unregister_x86_disassemble(void);

void plugin_run(void)
{
	register_x86_assemble();
	register_x86_disassemble();
}

void plugin_unload(void)
{
	unregister_x86_assemble();
	unregister_x86_disassemble();
}

}
```

---

## 3. Tool Registration & Lifecycles

Unlike the host application's built-in tools (which use the static `REGISTER_TOOL` macro to auto-register before `main`), plugins **MUST** register and unregister their tools programmatically inside `plugin_run` and `plugin_unload`.

> [!IMPORTANT]
> **Never use static initializers (`REGISTER_TOOL`) inside dynamic plugins.** Doing so results in duplicate registrations, static initialization crashes, and memory leaks because the plugin's static variables will be initialized and destroyed at times decoupled from the host's control.

### Programmatic Registration Pattern

To register a tool programmatically, call `tool_registry::get_instance().register_validator` and pass a factory lambda returning a unique pointer to the tool's validator.

```cpp
// inside x86_assemble_security.cpp
extern "C" {
void register_x86_assemble(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() {
		return std::make_unique<tools::x86_assemble_validator>();
	});
}

void unregister_x86_assemble(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("x86_assemble");
}
}
```

### Tool Family Activation Reasons

Plugins can also register and unregister activation reasons for their custom tool families. This allows the host application to dynamically display activation guidance (e.g. in the system prompt) when a family is inactive, rather than hardcoding it in the host binary.

To register a tool family reason, call `tool_registry::get_instance().register_tool_family(family, reason)` in `plugin_run` and `unregister_tool_family(family)` in `plugin_unload`.

```cpp
void plugin_run(void)
{
	...
	agentlib::tool_registry::get_instance().register_tool_family("x86", "Activate when working with x86 assembly");
}

void plugin_unload(void)
{
	...
	agentlib::tool_registry::get_instance().unregister_tool_family("x86");
}
```

---

## 4. Symbol Resolution and Linkage Rules

To prevent memory corruption, allocator crashes, and duplicate singleton instances, plugins must adhere to strict linkage rules:

### A. Avoid Statically Linking Host Libraries
Plugins must **NOT** link against `core_dep`, `agent_dep`, or `tools_dep` with `link_whole` or statically compile host source files. 
- In the plugin's `meson.build`, dependencies should only include external libraries (e.g. `zydis_dep`, `re2_dep`) or header-only dependencies (e.g. `nlohmann_json_dep`).
- Shared template implementations (like `std::map` or `std::function`) are dynamically linked against the system `libstdc++.so`.

### B. Export Host Symbols
The host executables (including the main `turbostar` target and unit tests like `test_pluginloader`) must be compiled with:
```meson
export_dynamic: true
```
This ensures that when a plugin is loaded with `dlopen`, the dynamic linker binds the plugin's references (e.g., to `tool_registry::get_instance()`) directly to the host's exported instances rather than creating duplicated copies inside the plugin.

---

## 5. Testing and Singleton Destruction Order

Static singletons in C++ are destroyed in the reverse order of their construction. In unit tests that load plugins:

1. If the host doesn't call `tool_registry::get_instance()` during startup, the `plugin_loader` singleton is constructed first.
2. When the plugin is loaded, it calls `tool_registry::get_instance()`, constructing the registry singleton second.
3. On process exit, the registry is destroyed first.
4. Then `plugin_loader` is destroyed, which calls `plugin_unload()` -> `unregister_validator()`.
5. Since `tool_registry` has already been destructed, this leads to a double-free / heap corruption crash.

> [!TIP]
> **Lifecycle Fix**: In test runners or host entry points that dynamically load plugins, always force the construction of `tool_registry` at the very beginning of `main()` to align the static lifecycle destruction order:
> ```cpp
> int main()
> {
>     // Force initialization of tool_registry before plugin_loader
>     (void)agentlib::tool_registry::get_instance();
> 
>     auto &loader = plugin_loader::get_instance();
>     loader.load_all_plugins();
>     ...
> }
> ```
