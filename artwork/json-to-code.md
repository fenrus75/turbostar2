# Recipe: Integrating a Durdraw Animation JSON

This document details the step-by-step process of embedding, registering, and driving a Durdraw `.json` animation in a plugin or core module within the TurboStar codebase.

---

## 1. Place the JSON File
Place your custom Durdraw animation JSON file under the target module or plugin directory.
- *Example*: For the `securityagent` plugin, the file is saved as `src/plugins/securityagent/security.json`.

---

## 2. Generate C++ Header in Build Configuration
Use the C++ text-embedding Python script (`scripts/embed_text.py`) within `meson.build` to generate a header file containing a raw array representation of the JSON string.

Define a `custom_target` in the relevant `meson.build` file (e.g., `src/plugins/meson.build` or `src/meson.build`):

```meson
# Define the target to generate the C++ array header
my_movie_h = custom_target('my_movie.h',
  input : 'securityagent/security.json',  # Path to the source JSON
  output : 'my_movie.h',                 # Output header name
  command : [
    embed_prog,
    join_paths(meson.project_source_root(), 'scripts/embed_text.py'),
    '@INPUT@',
    '@OUTPUT@',
    'my_movie_json'                       # C++ variable name for the array
  ]
)
```

---

## 3. Reference the Custom Target in the Build Sources
Add the custom target name (e.g. `my_movie_h`) directly to the sources list of the shared library, static library, or executable target:

```meson
securityagent_plugin = shared_module('securityagent',
  [
    'securityagent/plugin.cpp',
    # ... other cpp files ...
    my_movie_h  # Include custom target here
  ],
  dependencies: [nlohmann_json_dep],
  include_directories: inc,
  install: true,
  name_prefix: '',
  install_dir: plugin_dir
)
```

---

## 4. Include and Register the Animation in C++
In your plugin's initialization code (typically `plugin_run`):

1. Include the generated header and the animation registry header:
   ```cpp
   #include "agentlib/agent_animation.h"
   #include "my_movie.h"  // Name matches 'output' in custom_target
   ```
2. Register the animation JSON string:
   ```cpp
   void plugin_run(void)
   {
       // Register custom animation by name
       register_agent_animation("my_custom_anim", reinterpret_cast<const char *>(my_movie_json));
   }
   ```
3. Unregister the animation during cleanup (typically `plugin_unload`):
   ```cpp
   void plugin_unload(void)
   {
       // Clean up registry mapping on plugin unload
       unregister_agent_animation("my_custom_anim");
   }
   ```

---

## 5. Drive the Animation from an Agent
When spawning or creating the agent, specify the animation key using the agent's mutable `animation_name` property:

```cpp
auto subagent = parent->spawn_subagent("My Reviewer");
subagent->set_animation_name("my_custom_anim");
```

The presentation layer (`ui_agent_tile` / `ui_durmovie`) automatically:
- Resolves `"my_custom_anim"` using the thread-safe `agent_animation_registry`.
- Switches the animation frames dynamically at runtime.
- Falls back to the `"default"` movie if the animation key is not found or is unregistered upon plugin unload.
